from pathlib import Path

from cpp_deps_checker.model import Location, load
from cpp_deps_checker.rules import Context, Level, Violation, check
from tests.harness import FIXTURES, configure


def _violations(fixture: str, tmp_path: Path) -> list[Violation]:
    build = configure(FIXTURES / fixture, tmp_path / "build")
    return check(Context(model=load(build.dir, build.trace)))


def test_dependency_defined_after_its_consumer(tmp_path: Path) -> None:
    violations = _violations("definition_order_late", tmp_path)

    assert [(v.rule, v.level, v.location) for v in violations] == [
        ("definition-order", Level.ERROR, Location(Path("CMakeLists.txt"), 5))
    ]


def test_imported_dependency_is_exempt(tmp_path: Path) -> None:
    assert _violations("definition_order_imported", tmp_path) == []
