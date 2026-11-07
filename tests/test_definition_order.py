from pathlib import Path

from cpp_deps_checker.model import Location
from cpp_deps_checker.rules import Level
from tests.harness import check_fixture, summary


def test_dependency_defined_after_its_consumer(tmp_path: Path) -> None:
    assert summary(check_fixture("definition_order_late", tmp_path)) == [
        ("definition-order", Level.ERROR, Location(Path("CMakeLists.txt"), 5))
    ]


def test_imported_dependency_is_exempt(tmp_path: Path) -> None:
    assert check_fixture("definition_order_imported", tmp_path) == []
