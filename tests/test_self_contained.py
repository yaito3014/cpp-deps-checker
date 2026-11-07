from pathlib import Path

from cpp_deps_checker.model import Location
from cpp_deps_checker.rules import Level
from tests.harness import check_fixture, summary


def test_public_header_needing_a_private_include_directory(tmp_path: Path) -> None:
    violations = check_fixture("self_contained_private_include", tmp_path)

    assert summary(violations) == [
        ("self-contained", Level.ERROR, Location(Path("include/a/a.hpp"), 1))
    ]
    assert violations[0].message == (
        "header of a does not preprocess on its own: 'config.hpp' file not found"
    )
