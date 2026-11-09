from pathlib import Path

from cpp_deps_checker.model import Location
from cpp_deps_checker.rules import Level
from tests.harness import check_fixture, summary


def test_include_of_transitive_only_dependency(tmp_path: Path) -> None:
    violations = check_fixture("direct_layering_transitive", tmp_path)

    assert summary(violations) == [
        ("direct-layering", Level.ERROR, Location(Path("a/a.cpp"), 2))
    ]


def test_public_header_including_private_dependency(tmp_path: Path) -> None:
    violations = check_fixture("direct_layering_public_private", tmp_path)

    # The PRIVATE link also keeps c's include directory from a's header set
    # verification, so the header does not preprocess on its own either.
    header = Location(Path("a/include/a/a.hpp"), 3)
    assert summary(violations) == [
        ("direct-layering", Level.ERROR, header),
        ("self-contained", Level.ERROR, header),
    ]
    assert "PUBLIC" in violations[0].message
    assert violations[1].message == (
        "header of a does not preprocess on its own: 'c/c.hpp' file not found"
    )


def test_public_header_including_public_dependency(tmp_path: Path) -> None:
    assert check_fixture("direct_layering_public_public", tmp_path) == []
