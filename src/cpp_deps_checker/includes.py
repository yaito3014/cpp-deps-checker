import functools
import json
import re
import shlex
import subprocess
from collections.abc import Callable, Iterable, Sequence
from concurrent.futures import ThreadPoolExecutor
from dataclasses import dataclass
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
# #line, or its GNU form `# 12 "file"`, in source text.
_RENUMBERING = re.compile(r"^[ \t]*#[ \t]*(?:line\b|[0-9])", re.MULTILINE)


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


@dataclass
class _Pending:
    includer: Path | None  # None in the predefines or on the command line
    line: int
    forced: bool  # inside a header included with -include
    entered: Path | None = None


def parse_directives(
    output: str, directory: Path
) -> tuple[list[Directive], list[Include]]:
    """The include directives in the output of `clang -E -dI`, in order, and
    the include edges of the headers included with -include.

    Line markers give the file and line of the next output line, and each
    other line is the next line of that file; their flags tell when a file
    is entered (1) and when it is returned to (2). -H prints neither a
    -include header nor what it includes, so the directives under one are
    not returned but give the edges to the headers they entered, the header
    itself counting as included by the main file without a line. A header
    such a directive skipped (include guard, #pragma once) is not named, so
    it gives no edge.
    """
    found: list[_Pending] = []
    stack: list[str] = []  # files entered, the main file first
    main: Path | None = None
    file: Path | None = None
    line = 0
    # The directive whose header may be entered next, with only line markers
    # in between.
    pending: _Pending | None = None
    for text in output.splitlines():
        marker = _MARKER.match(text)
        if marker is not None:
            name = _ESCAPE.sub(r"\1", marker[2])
            path = None if name.startswith("<") else (directory / name).resolve()
            key = name if path is None else str(path)
            flags = marker[3].split()
            if not stack:
                stack, main = [key], path
            elif "1" in flags:
                if pending is not None:
                    pending.entered = path
                pending = None
                stack.append(key)
            elif "2" in flags:
                while len(stack) > 1 and stack[-1] != key:
                    stack.pop()
            file, line = path, int(marker[1])
            continue
        pending = None
        if _DIRECTIVE.match(text):
            forced = any(entry.startswith("<") for entry in stack[1:])
            pending = _Pending(file, line, forced)
            found.append(pending)
        line += 1
    directives = [
        Directive(d.includer, d.line, d.entered)
        for d in found
        if not d.forced and d.includer is not None
    ]
    forced_includes = [
        Include(d.includer, d.entered, d.line)
        if d.includer is not None
        else Include(main, d.entered, None)
        for d in found
        if d.forced and d.entered is not None and main is not None
    ]
    return directives, forced_includes


def directive_lines(
    edges: Sequence[_Edge],
    directives: Sequence[Directive],
    error: Diagnostic | None,
    renumbers: Callable[[Path], bool] = lambda _: False,
) -> list[int | None]:
    """The line of each edge from parse_tree, paired in order with the
    directive that made it.

    The directive at the location of the translation unit's first error is
    left out, since -H prints nothing for a header it did not find. An edge
    whose includer renumbers its lines (#line) gets no line, since line
    markers carry the renumbered lines, and is paired without comparing
    includers, since #line may also rename the file. If the two sequences
    still disagree on a count, an includer, or an entered header, no edge
    gets a line rather than a wrong one.
    """
    if error is not None:
        directives = [
            d
            for d in directives
            if d.entered is not None or (d.includer, d.line) != (error.file, error.line)
        ]
    if len(directives) != len(edges) or any(
        (d.includer != includer and not renumbers(includer))
        or d.entered not in {None, included}
        for d, (includer, included) in zip(directives, edges, strict=True)
    ):
        return [None] * len(edges)
    return [
        None if renumbers(includer) else d.line
        for d, (includer, _) in zip(directives, edges, strict=True)
    ]


@functools.cache
def _renumbers(path: Path) -> bool:
    """Whether the file may renumber its lines, even in a comment or a branch
    that is not compiled."""
    return _RENUMBERING.search(path.read_text(errors="replace")) is not None


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
    directives, forced_includes = parse_directives(result.stdout, directory)
    lines = directive_lines(edges, directives, error, _renumbers)
    includes = [
        Include(includer, included, line)
        for (includer, included), line in zip(edges, lines, strict=True)
    ] + [
        Include(i.includer, i.included, None) if _renumbers(i.includer) else i
        for i in forced_includes
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
