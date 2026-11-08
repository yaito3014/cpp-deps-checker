import re
from collections.abc import Mapping
from dataclasses import dataclass
from enum import StrEnum
from pathlib import Path

from cpp_deps_checker.model import Location, Model
from cpp_deps_checker.rules import Violation


def text(violation: Violation) -> str:
    """One compiler-style line: file:line: level: message [rule]."""
    prefix = f"{violation.location}: " if violation.location else ""
    return f"{prefix}{violation.level}: {violation.message} [{violation.rule}]"


def _from_root(location: Location, source_dir: Path, root: Path) -> Location:
    """location with its file relative to root if inside it, absolute if not."""
    path = source_dir / location.file
    file = path.relative_to(root) if path.is_relative_to(root) else path
    return Location(file, location.line)


# Workflow command escaping, as in @actions/core's command.ts.
def _escape_data(value: str) -> str:
    return value.replace("%", "%25").replace("\r", "%0D").replace("\n", "%0A")


def _escape_property(value: str) -> str:
    return _escape_data(value).replace(":", "%3A").replace(",", "%2C")


def annotation(violation: Violation, source_dir: Path, root: Path) -> str:
    """A GitHub Actions ::error or ::warning workflow command.

    GitHub attaches an annotation to a file by its path relative to the
    repository root, which root names. A location outside root goes into the
    message instead.
    """
    properties: list[tuple[str, str]] = []
    message = violation.message
    if violation.location is not None:
        location = _from_root(violation.location, source_dir, root)
        if location.file.is_absolute():
            message = f"{location}: {message}"
        else:
            properties.append(("file", str(location.file)))
            if location.line is not None:
                properties.append(("line", str(location.line)))
    properties.append(("title", violation.rule))
    joined = ",".join(f"{k}={_escape_property(v)}" for k, v in properties)
    return f"::{violation.level} {joined}::{_escape_data(message)}"


class GraphError(Exception):
    """A file is not a graph that mermaid wrote."""


class Kind(StrEnum):
    PRODUCTION = "production"
    TEST = "test"
    IMPORTED = "imported"


type Edge = tuple[str, str]  # consumer name, dependency name


@dataclass(frozen=True)
class Graph:
    nodes: Mapping[str, Kind]  # by target name
    edges: frozenset[Edge]


def graph(model: Model, tests: frozenset[str]) -> Graph:
    """The declared edges between targets, by name.

    Header set verification targets are left out because CMake creates them,
    and UTILITY targets because CMake does not let them be linked. Targets
    that share a name (imported targets in different directories) share a
    node.
    """
    shown = {
        t.id: t
        for t in model.targets.values()
        if t.verifies is None and t.type != "UTILITY"
    }

    def kind(target_id: str) -> Kind:
        if shown[target_id].imported:
            return Kind.IMPORTED
        return Kind.TEST if target_id in tests else Kind.PRODUCTION

    return Graph(
        nodes={t.name: kind(t.id) for t in shown.values()},
        edges=frozenset(
            (t.name, shown[d.target].name)
            for t in shown.values()
            for d in t.declared_dependencies
            if d.target in shown
        ),
    )


_FENCE = "```"
_HEADER = (
    f"{_FENCE}mermaid",
    "flowchart TD",
    "  classDef test stroke-dasharray: 4 4",
    "  classDef imported font-style: italic",
)
_NODE = re.compile(r'^  (n\d+)\["([^"]*)"\](?::::(test|imported))?$')
_EDGE = re.compile(r"^  (n\d+) --> (n\d+)$")
# Mermaid entity codes, for the characters that would end the label or
# start an entity.
_ESCAPES = {"#": "#35;", '"': "#quot;"}
_ESCAPED = re.compile("|".join(re.escape(e) for e in _ESCAPES.values()))
_UNESCAPES = {v: k for k, v in _ESCAPES.items()}


def mermaid(graph: Graph) -> str:
    """A Markdown file holding the graph as a Mermaid flowchart, consumers
    above their dependencies, in a stable order."""
    names = sorted(graph.nodes)
    ids = {name: f"n{i}" for i, name in enumerate(names)}
    lines = list(_HEADER)
    for name in names:
        label = "".join(_ESCAPES.get(c, c) for c in name)
        kind = graph.nodes[name]
        suffix = "" if kind is Kind.PRODUCTION else f":::{kind}"
        lines.append(f'  {ids[name]}["{label}"]{suffix}')
    lines.extend(f"  {ids[a]} --> {ids[b]}" for a, b in sorted(graph.edges))
    lines.append(_FENCE)
    return "".join(f"{line}\n" for line in lines)


def parse_mermaid(text: str) -> Graph:
    """The graph in a file that mermaid wrote."""
    lines = text.splitlines()
    if tuple(lines[: len(_HEADER)]) != _HEADER or lines[-1:] != [_FENCE]:
        raise GraphError("not a Mermaid graph written by cpp-deps-checker")
    names: dict[str, str] = {}
    nodes: dict[str, Kind] = {}
    edges: set[Edge] = set()
    for number, line in enumerate(lines[len(_HEADER) : -1], start=len(_HEADER) + 1):
        if node := _NODE.match(line):
            name = _ESCAPED.sub(lambda m: _UNESCAPES[m[0]], node[2])
            names[node[1]] = name
            nodes[name] = Kind(node[3] or Kind.PRODUCTION)
        elif (edge := _EDGE.match(line)) and edge[1] in names and edge[2] in names:
            edges.add((names[edge[1]], names[edge[2]]))
        else:
            raise GraphError(f"line {number} is not a node or an edge: {line}")
    return Graph(nodes=nodes, edges=frozenset(edges))
