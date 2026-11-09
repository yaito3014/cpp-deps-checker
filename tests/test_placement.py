from pathlib import Path

from cpp_deps_checker.model import Location
from cpp_deps_checker.rules import Level
from tests.factories import messages, placed
from tests.harness import check_fixture, summary


def test_target_placed_above_its_only_consumer(tmp_path: Path) -> None:
    violations = check_fixture("placement_above_consumers", tmp_path)

    assert summary(violations) == [
        ("placement", Level.WARNING, Location(Path("lexer/CMakeLists.txt"), 1))
    ]
    assert violations[0].message == (
        "every path to lexer goes through parser; move lexer under parser/"
    )


def test_dominator_is_the_lowest_common_ancestor_of_consumers() -> None:
    # ast is reached through both parser and printer, so its dominator is
    # app; token is also reached from parser directly, so app as well.
    assert (
        messages(
            "placement",
            placed("app", ".", "parser", "printer"),
            placed("parser", "parser", "ast", "token"),
            placed("printer", "printer", "ast"),
            placed("ast", "ast", "token"),
            placed("token", "token"),
        )
        == []
    )


def test_nested_under_the_dominator_is_fine() -> None:
    assert (
        messages(
            "placement",
            placed("app", ".", "parser"),
            placed("parser", "parser", "lexer"),
            placed("lexer", "parser/lexer"),
        )
        == []
    )


def test_same_directory_as_the_dominator_is_fine() -> None:
    assert (
        messages(
            "placement",
            placed("app", ".", "parser"),
            placed("parser", "parser", "parser_obj"),
            placed("parser_obj", "parser"),
        )
        == []
    )


def test_target_shared_by_two_roots_has_no_placement() -> None:
    assert (
        messages(
            "placement",
            placed("cli", "cli", "core"),
            placed("gui", "gui", "core"),
            placed("core", "core"),
        )
        == []
    )


def test_each_unconsumed_target_is_a_root() -> None:
    # lib is a root of its own; util is reached through lib only.
    assert messages(
        "placement", placed("lib", "lib", "util"), placed("util", "util")
    ) == ["every path to util goes through lib; move util under lib/"]


def test_dominator_through_a_diamond() -> None:
    # app -> {a, b} -> c -> d: c and d are dominated by app, d by c.
    assert messages(
        "placement",
        placed("app", "app", "a", "b"),
        placed("a", "app/a", "c"),
        placed("b", "app/b", "c"),
        placed("c", "app/c", "d"),
        placed("d", "d"),
    ) == ["every path to d goes through c; move d under app/c/"]


def test_cycle_reached_from_a_root() -> None:
    assert messages(
        "placement",
        placed("app", "app", "a"),
        placed("a", "app/a", "b"),
        placed("b", "b", "a"),
    ) == ["every path to b goes through a; move b under app/a/"]


def test_tests_and_verification_targets_are_not_consumers() -> None:
    assert messages(
        "placement",
        placed("lib", "lib", "util"),
        placed("util", "util"),
        placed("util_test", "tests", "util"),
        placed("util_verify", "util", "util", verifies="util"),
    ) == ["every path to util goes through lib; move util under lib/"]
