from pathlib import Path

from cpp_deps_checker.model import Location, Model, load
from cpp_deps_checker.rules import Context, Level, check
from tests import factories
from tests.factories import target
from tests.harness import FIXTURES, configure


def _model(edges: dict[str, list[str]]) -> Model:
    """A model whose targets declare only the given edges, by name."""
    return factories.model(*(target(n, links=ts) for n, ts in edges.items()))


def test_static_libraries_linking_each_other(tmp_path: Path) -> None:
    build = configure(FIXTURES / "cycles_static_pair", tmp_path / "build")
    violations = check(Context(model=load(build.dir, build.trace)))

    # Every cycle has an edge to a later-defined target, so rule 2 fires too.
    assert [(v.rule, v.level, v.location) for v in violations] == [
        ("no-cycles", Level.ERROR, Location(Path("CMakeLists.txt"), 5)),
        ("definition-order", Level.ERROR, Location(Path("CMakeLists.txt"), 5)),
    ]
    (cycle,) = (v for v in violations if v.rule == "no-cycles")
    assert "a -> b -> a" in cycle.message


def test_acyclic_diamond_is_clean() -> None:
    model = _model({"a": ["b", "c"], "b": ["d"], "c": ["d"], "d": []})
    assert check(Context(model=model)) == []


def test_one_violation_per_component_with_shortest_cycle() -> None:
    model = _model({"a": ["b"], "b": ["c", "a"], "c": ["a"], "d": ["e"], "e": ["d"]})

    messages = [v.message for v in check(Context(model=model))]

    assert messages == [
        "dependency cycle: a -> b -> a (also strongly connected: c)",
        "dependency cycle: d -> e -> d",
    ]
