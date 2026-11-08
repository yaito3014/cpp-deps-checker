from pathlib import Path

from cpp_deps_checker.model import Location
from cpp_deps_checker.rules import Level
from tests.factories import messages, placed
from tests.harness import check_fixture, summary


def test_consumer_outside_the_parent_directory(tmp_path: Path) -> None:
    violations = check_fixture("nesting_outside_parent", tmp_path)

    assert summary(violations) == [
        ("nesting", Level.ERROR, Location(Path("CMakeLists.txt"), 5))
    ]
    assert violations[0].message == (
        "app links lexer, which is private to parser/; "
        "hoist lexer: a private detail is becoming shared API"
    )


def test_consumers_inside_the_parent_directory_are_fine() -> None:
    assert (
        messages(
            "nesting",
            placed("parser", "parser", "lexer"),
            placed("lexer", "parser/lexer"),
            placed("parser_extra", "parser/extra", "lexer"),
        )
        == []
    )


def test_parent_is_the_nearest_directory_defining_a_target() -> None:
    # parser/impl defines nothing, so lexer is private to parser/.
    found = messages(
        "nesting",
        placed("parser", "parser"),
        placed("lexer", "parser/impl/lexer"),
        placed("app", "app", "lexer"),
    )
    assert found == [
        "app links lexer, which is private to parser/; "
        "hoist lexer: a private detail is becoming shared API"
    ]


def test_targets_without_a_defining_ancestor_are_shared() -> None:
    assert (
        messages(
            "nesting",
            placed("docs", "libs", type="UTILITY"),
            placed("lexer", "libs/lexer"),
            placed("app", "app", "lexer"),
        )
        == []
    )


def test_top_level_directory_constrains_nothing() -> None:
    assert (
        messages(
            "nesting",
            placed("top", "."),
            placed("lexer", "libs/lexer"),
            placed("app", "app", "lexer"),
        )
        == []
    )


def test_test_targets_are_exempt_both_ways() -> None:
    assert (
        messages(
            "nesting",
            placed("parser", "parser"),
            placed("lexer", "parser/lexer"),
            placed("lexer_test", "tests", "lexer"),
            placed("tests_base", "tests"),
            placed("helper", "tests/unit/helper"),
            placed("other_test", "tests/other", "helper"),
        )
        == []
    )


def test_verification_and_imported_targets_are_exempt() -> None:
    assert (
        messages(
            "nesting",
            placed("parser", "parser"),
            placed("lexer", "parser/lexer"),
            placed("v", "elsewhere", "lexer", verifies="lexer"),
            placed("ext", "elsewhere", "lexer", imported=True),
        )
        == []
    )
