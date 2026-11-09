import json
import re
import shlex
import subprocess
from collections.abc import Callable, Iterable, Sequence
from concurrent.futures import ThreadPoolExecutor
from dataclasses import dataclass, replace
from pathlib import Path
from typing import Any

type _Edge = tuple[Path, Path]  # includer, included

_TREE_LINE = re.compile(r"^(\.+) (.+)$")
# A line marker of `clang -E`: line, file, and flags (1 entering a file).
_MARKER = re.compile(r'^# (\d+) "((?:[^"\\]|\\.)*)"((?: \d+)*)$')
_ESCAPE = re.compile(r"\\(.)")
# An include directive that `clang -E -dI` kept in its output.
_DIRECTIVE = re.compile(r"^#(?:include|include_next|import) .* /\* clang -E -dI \*/$")
_ERROR = re.compile(r"^(.+):(\d+):\d+: (?:fatal )?error: (.+)$")


class IncludeError(Exception):
    """A translation unit could not be preprocessed."""


@dataclass(frozen=True, order=True)
class Include:
    includer: Path  # absolute and resolved, like included
    included: Path
    line: int | None  # of the directive in includer, if it could be found


@dataclass(frozen=True)
class Diagnostic:
    file: Path  # absolute and resolved
    line: int
    message: str


@dataclass(frozen=True)
class Failure:
    """A translation unit that did not preprocess."""

    source: Path  # absolute and resolved
    error: Diagnostic | None  # the first error the compiler reported
    output: str  # everything the compiler wrote to stderr


@dataclass(frozen=True)
class Collection:
    # A failed translation unit still contributes the edges it printed before
    # stopping.
    includes: tuple[Include, ...]
    failures: tuple[Failure, ...]


def parse_tree(output: str, source: Path, directory: Path) -> list[_Edge]:
    """Edges of the include tree that `clang -H` printed for source, one per
    printed header, in order.

    Each line is one dot per nesting level, a space, and the path as the
    compiler opened it, relative to the compile directory if not absolute.
    Lines without leading dots (such as the include guard hints) are skipped.
    """
    stack = [(directory / source).resolve()]
    edges: list[_Edge] = []
    for line in output.splitlines():
        match = _TREE_LINE.match(line)
        if match is None:
            continue
        depth = len(match[1])
        header = (directory / match[2]).resolve()
        del stack[depth:]
        edges.append((stack[-1], header))
        stack.append(header)
    return edges


@dataclass(frozen=True)
class Directive:
    """An include directive the preprocessor ran."""

    includer: Path  # absolute and resolved
    line: int
    # The header the preprocessor then entered; None if it skipped the header
    # (include guard, #pragma once) or did not find it.
    entered: Path | None


def parse_directives(output: str, directory: Path) -> list[Directive]:
    """The include directives in the output of `clang -E -dI`, in order.

    Line markers give the file and line of the next output line, and each
    other line is the next line of that file. Directives of the predefines
    and the command line (-include) are left out, since -H does not print
    the headers they include.
    """
    directives: list[Directive] = []
    file: Path | None = None
    line = 0
    # A directive whose header is entered right after it, with only line
    # markers in between.
    pending: int | None = None
    for text in output.splitlines():
        marker = _MARKER.match(text)
        if marker is not None:
            name = _ESCAPE.sub(r"\1", marker[2])
            path = None if name.startswith("<") else (directory / name).resolve()
            if "1" in marker[3].split():
                if pending is not None and path is not None:
                    directives[pending] = replace(directives[pending], entered=path)
                pending = None
            file, line = path, int(marker[1])
            continue
        pending = None
        if file is not None and _DIRECTIVE.match(text):
            directives.append(Directive(file, line, None))
            pending = len(directives) - 1
        line += 1
    return directives


def directive_lines(
    edges: Sequence[_Edge],
    directives: Sequence[Directive],
    error: Diagnostic | None,
) -> list[int | None]:
    """The line of each edge from parse_tree, paired in order with the
    directive that made it.

    The directive at the location of the translation unit's first error is
    left out, since -H prints nothing for a header it did not find. If the
    two sequences still disagree on a count, an includer, or an entered
    header, no edge gets a line rather than a wrong one.
    """
    if error is not None:
        directives = [
            d
            for d in directives
            if d.entered is not None or (d.includer, d.line) != (error.file, error.line)
        ]
    if len(directives) != len(edges) or any(
        d.includer != includer or d.entered not in {None, included}
        for d, (includer, included) in zip(directives, edges, strict=True)
    ):
        return [None] * len(edges)
    return [d.line for d in directives]


def _preprocess_arguments(entry: dict[str, Any]) -> list[str]:
    arguments = (
        entry["arguments"] if "arguments" in entry else shlex.split(entry["command"])
    )
    kept: list[str] = []
    rest = iter(arguments)
    for argument in rest:
        if argument == "-o":
            next(rest, None)
        elif argument != "-c":
            kept.append(argument)
    # -fshow-skipped-includes also prints headers skipped by their include
    # guard, which plain -H omits; that edge is needed all the same. -dI keeps
    # the directives in the output, which tells where each one is.
    return [*kept, "-E", "-dI", "-H", "-fshow-skipped-includes"]


def _first_error(output: str, directory: Path) -> Diagnostic | None:
    for line in output.splitlines():
        match = _ERROR.match(line)
        if match is not None:
            file = (directory / match[1]).resolve()
            return Diagnostic(file, int(match[2]), match[3])
    return None


def _preprocess(entry: dict[str, Any]) -> tuple[list[Include], Failure | None]:
    directory = Path(entry["directory"])
    source = Path(entry["file"])
    result = subprocess.run(
        _preprocess_arguments(entry),
        cwd=directory,
        capture_output=True,
        text=True,
        errors="replace",
        check=False,
    )
    edges = parse_tree(result.stderr, source, directory)
    error = _first_error(result.stderr, directory)
    lines = directive_lines(edges, parse_directives(result.stdout, directory), error)
    includes = [
        Include(includer, included, line)
        for (includer, included), line in zip(edges, lines, strict=True)
    ]
    if result.returncode == 0:
        return includes, None
    failure = Failure(
        source=(directory / source).resolve(), error=error, output=result.stderr
    )
    return includes, failure


def _deduplicated(includes: Iterable[Include]) -> set[Include]:
    """Each include once, and an edge without a line only if no translation
    unit found its line."""
    unique = set(includes)
    found = {(i.includer, i.included) for i in unique if i.line is not None}
    return {
        i for i in unique if i.line is not None or (i.includer, i.included) not in found
    }


def collect(
    compile_commands: Path,
    keep: Callable[[Path], bool],
    skip: Callable[[Path], bool] = lambda _: False,
) -> Collection:
    """Include edges, for includers that keep accepts, and the translation
    units that failed to preprocess; a translation unit whose source skip
    accepts is not preprocessed."""
    entries = [
        entry
        for entry in json.loads(compile_commands.read_text())
        if not skip((Path(entry["directory"]) / entry["file"]).resolve())
    ]
    with ThreadPoolExecutor() as pool:
        results = list(pool.map(_preprocess, entries))
    includes = _deduplicated(i for includes, _ in results for i in includes)
    return Collection(
        includes=tuple(sorted(i for i in includes if keep(i.includer))),
        failures=tuple(failure for _, failure in results if failure is not None),
    )
