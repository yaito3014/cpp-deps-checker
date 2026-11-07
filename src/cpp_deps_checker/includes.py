import json
import os
import re
import shlex
import subprocess
from collections.abc import Callable, Iterable
from concurrent.futures import ThreadPoolExecutor
from dataclasses import dataclass
from pathlib import Path
from typing import Any

type _Edge = tuple[Path, Path]  # includer, included

_TREE_LINE = re.compile(r"^(\.+) (.+)$")
_DIRECTIVE = re.compile(r'^\s*#\s*(?:include|include_next|import)\s*[<"]([^>"]+)[>"]')
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


def parse_tree(output: str, source: Path, directory: Path) -> set[_Edge]:
    """Edges of the include tree that `clang -H` printed for source.

    Each line is one dot per nesting level, a space, and the path as the
    compiler opened it, relative to the compile directory if not absolute.
    Lines without leading dots (such as the include guard hints) are skipped.
    """
    stack = [(directory / source).resolve()]
    edges: set[_Edge] = set()
    for line in output.splitlines():
        match = _TREE_LINE.match(line)
        if match is None:
            continue
        depth = len(match[1])
        header = (directory / match[2]).resolve()
        del stack[depth:]
        edges.add((stack[-1], header))
        stack.append(header)
    return edges


def directive_line(text: str, included: Path) -> int | None:
    """Line of the #include in text whose spelled path best matches included."""
    best: tuple[int, int] | None = None  # spelled length, line
    for number, line in enumerate(text.splitlines(), start=1):
        match = _DIRECTIVE.match(line)
        if match is None:
            continue
        spelled = Path(match[1]).parts
        if included.parts[-len(spelled) :] == spelled and (
            best is None or len(spelled) > best[0]
        ):
            best = (len(spelled), number)
    return None if best is None else best[1]


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
    # guard, which plain -H omits; that edge is needed all the same.
    return [*kept, "-E", "-H", "-fshow-skipped-includes", "-o", os.devnull]


def _first_error(output: str, directory: Path) -> Diagnostic | None:
    for line in output.splitlines():
        match = _ERROR.match(line)
        if match is not None:
            file = (directory / match[1]).resolve()
            return Diagnostic(file, int(match[2]), match[3])
    return None


def _preprocess(entry: dict[str, Any]) -> tuple[set[_Edge], Failure | None]:
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
    if result.returncode == 0:
        return edges, None
    failure = Failure(
        source=(directory / source).resolve(),
        error=_first_error(result.stderr, directory),
        output=result.stderr,
    )
    return edges, failure


def _lines(edges: Iterable[_Edge]) -> list[Include]:
    texts: dict[Path, str] = {}
    includes: list[Include] = []
    for includer, included in edges:
        if includer not in texts:
            texts[includer] = includer.read_text(errors="replace")
        includes.append(
            Include(includer, included, directive_line(texts[includer], included))
        )
    return includes


def collect(compile_commands: Path, keep: Callable[[Path], bool]) -> Collection:
    """Include edges of every translation unit, for includers that keep accepts,
    and the translation units that failed to preprocess."""
    entries = json.loads(compile_commands.read_text())
    with ThreadPoolExecutor() as pool:
        results = list(pool.map(_preprocess, entries))
    edges: set[_Edge] = set().union(*(edges for edges, _ in results))
    return Collection(
        includes=tuple(sorted(_lines(edge for edge in edges if keep(edge[0])))),
        failures=tuple(failure for _, failure in results if failure is not None),
    )
