import sys
from pathlib import Path

from cpp_deps_checker.model import Location
from cpp_deps_checker.rules import Level
from tests import factories
from tests.factories import target
from tests.harness import check_fixture, summary


def _check(edges: dict[str, list[str]]) -> list[str]:
    """Cycle messages of a model whose targets declare only the given edges."""
    return factories.messages(
        "no-cycles", *(target(n, links=ts) for n, ts in edges.items())
    )


def test_static_libraries_linking_each_other(tmp_path: Path) -> None:
    violations = check_fixture("cycles_static_pair", tmp_path)

    # Every cycle has an edge to a later-defined target, so rule 2 fires too.
    assert summary(violations) == [
        ("no-cycles", Level.ERROR, Location(Path("CMakeLists.txt"), 5)),
        ("definition-order", Level.ERROR, Location(Path("CMakeLists.txt"), 5)),
    ]
    (cycle,) = (v for v in violations if v.rule == "no-cycles")
    assert "a -> b -> a" in cycle.message


def test_acyclic_diamond_is_clean() -> None:
    assert _check({"a": ["b", "c"], "b": ["d"], "c": ["d"], "d": []}) == []


def test_one_violation_per_component_with_shortest_cycle() -> None:
    messages = _check({"a": ["b"], "b": ["c", "a"], "c": ["a"], "d": ["e"], "e": ["d"]})

    assert messages == [
        "dependency cycle: a -> b -> a (also strongly connected: c)",
        "dependency cycle: d -> e -> d",
    ]


def test_cycle_closed_only_through_a_dependency_of_a_dependency() -> None:
    assert _check({"a": ["b"], "b": ["c"], "c": ["a"]}) == [
        "dependency cycle: a -> b -> c -> a"
    ]


def test_chain_deeper_than_the_recursion_limit() -> None:
    n = sys.getrecursionlimit() + 100
    edges = {f"t{i}": [f"t{i + 1}"] for i in range(n)}
    edges[f"t{n}"] = [f"t{n - 1}"]

    assert _check(edges) == [f"dependency cycle: t{n - 1} -> t{n} -> t{n - 1}"]
