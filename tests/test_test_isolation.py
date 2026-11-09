from pathlib import Path

from cpp_deps_checker.model import Location
from cpp_deps_checker.rules import Level
from tests.harness import check_fixture, summary


def test_production_target_linking_a_test_helper(tmp_path: Path) -> None:
    violations = check_fixture("test_isolation_helper", tmp_path)

    assert summary(violations) == [
        ("test-isolation", Level.ERROR, Location(Path("app/CMakeLists.txt"), 2))
    ]
    assert violations[0].message == (
        "app links test target test_helper; only test targets may link it"
    )
