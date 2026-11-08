from pathlib import Path

from cpp_deps_checker.model import Location
from cpp_deps_checker.rules import Level
from tests.harness import check_fixture, summary


def test_production_target_including_a_detail_header(tmp_path: Path) -> None:
    violations = check_fixture("entry_point_detail", tmp_path)

    assert summary(violations) == [
        ("entry-point", Level.ERROR, Location(Path("b/b.cpp"), 2))
    ]
    assert violations[0].message == (
        "b includes a/include/a/detail/impl.hpp, a detail header of a; "
        "only a and test targets may include it"
    )


def test_test_target_including_a_detail_header(tmp_path: Path) -> None:
    assert check_fixture("entry_point_detail_test", tmp_path) == []


def test_test_target_including_a_private_header(tmp_path: Path) -> None:
    violations = check_fixture("entry_point_private_test", tmp_path)

    assert summary(violations) == [
        ("entry-point", Level.ERROR, Location(Path("tests/a_test.cpp"), 1))
    ]
    assert violations[0].message == (
        "a_test includes a/src/impl.hpp, a private header of a; only a may include it"
    )
