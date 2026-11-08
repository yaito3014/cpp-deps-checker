from pathlib import Path

import pytest

from cpp_deps_checker import report
from cpp_deps_checker.model import Location
from cpp_deps_checker.report import Graph, Kind
from cpp_deps_checker.rules import Level, Violation
from tests.factories import model, placed

SOURCE = Path("/repo/project")
ROOT = Path("/repo")


def _violation(
    location: Location | None, message: str = "a links b", level: Level = Level.ERROR
) -> Violation:
    return Violation("no-cycles", level, message, location)


def test_annotation_path_is_relative_to_root() -> None:
    violation = _violation(Location(Path("a/CMakeLists.txt"), 3))

    assert report.annotation(violation, SOURCE, ROOT) == (
        "::error file=project/a/CMakeLists.txt,line=3,title=no-cycles::a links b"
    )


def test_annotation_without_line_or_location() -> None:
    no_line = _violation(Location(Path("a.hpp"), None), level=Level.WARNING)

    assert report.annotation(no_line, SOURCE, ROOT) == (
        "::warning file=project/a.hpp,title=no-cycles::a links b"
    )
    assert report.annotation(_violation(None), SOURCE, ROOT) == (
        "::error title=no-cycles::a links b"
    )


def test_annotation_outside_root_puts_location_in_message() -> None:
    violation = _violation(Location(Path("/usr/include/x.hpp"), 7))

    assert report.annotation(violation, SOURCE, ROOT) == (
        "::error title=no-cycles::/usr/include/x.hpp:7: a links b"
    )


def test_annotation_escapes_data_and_properties() -> None:
    violation = _violation(Location(Path("a,b:c%.hpp"), 1), "50% done\r\nnext")

    assert report.annotation(violation, SOURCE, ROOT) == (
        "::error file=project/a%2Cb%3Ac%25.hpp,line=1,title=no-cycles"
        "::50%25 done%0D%0Anext"
    )


def test_graph_has_declared_targets_and_edges_by_name() -> None:
    m = model(
        placed("app", "", "lib", "ext::x", type="EXECUTABLE"),
        placed("lib", "lib"),
        placed("ext::x", "", imported=True),
        placed("lib_test", "tests", "lib", type="EXECUTABLE"),
        placed("lib_verify_interface_header_sets", "lib", "lib", verifies="lib"),
        placed("docs", "docs", type="UTILITY"),
        # Every edge ends at a node, even one CMake would reject.
        placed("odd", "odd", "lib_verify_interface_header_sets", "docs"),
    )

    assert report.graph(m, frozenset({"lib_test"})) == Graph(
        nodes={
            "app": Kind.PRODUCTION,
            "ext::x": Kind.IMPORTED,
            "lib": Kind.PRODUCTION,
            "lib_test": Kind.TEST,
            "odd": Kind.PRODUCTION,
        },
        edges=frozenset({("app", "lib"), ("app", "ext::x"), ("lib_test", "lib")}),
    )


def test_mermaid_lists_sorted_nodes_then_edges() -> None:
    graph = Graph(
        nodes={"b": Kind.PRODUCTION, "a": Kind.TEST, "ext::x": Kind.IMPORTED},
        edges=frozenset(
            {("b", "ext::x"), ("ext::x", "a"), ("a", "b"), ("b", "a"), ("a", "ext::x")}
        ),
    )

    assert report.mermaid(graph) == (
        "```mermaid\n"
        "flowchart TD\n"
        "  classDef test stroke-dasharray: 4 4\n"
        "  classDef imported font-style: italic\n"
        '  n0["a"]:::test\n'
        '  n1["b"]\n'
        '  n2["ext::x"]:::imported\n'
        "  n0 --> n1\n"
        "  n0 --> n2\n"
        "  n1 --> n0\n"
        "  n1 --> n2\n"
        "  n2 --> n0\n"
        "```\n"
    )


_EMPTY = report.mermaid(Graph(nodes={}, edges=frozenset()))
_HEADER = _EMPTY.removesuffix("```\n")


def test_empty_graph_round_trips() -> None:
    assert report.parse_mermaid(_EMPTY) == Graph(nodes={}, edges=frozenset())


def test_mermaid_round_trips() -> None:
    graph = Graph(
        nodes={
            'q"uote': Kind.PRODUCTION,
            "h#sh": Kind.TEST,
            "#quot;": Kind.IMPORTED,
            "lone": Kind.PRODUCTION,
        },
        edges=frozenset({('q"uote', "h#sh"), ("h#sh", "#quot;")}),
    )

    assert report.parse_mermaid(report.mermaid(graph)) == graph


@pytest.mark.parametrize(
    "text",
    [
        "",
        "# Graph\n",
        _EMPTY.replace("flowchart TD", "flowchart LR"),
        _HEADER + '  n0["a"]\n  n0 --> n1\n```\n',
        _HEADER + '  n0["a"]\n  n0 -> n0\n```\n',
        _HEADER + '  n0["a"]:::other\n```\n',
        _HEADER + '  n0["a"]\n',
    ],
)
def test_parse_mermaid_rejects_what_mermaid_did_not_write(text: str) -> None:
    with pytest.raises(report.GraphError):
        report.parse_mermaid(text)
