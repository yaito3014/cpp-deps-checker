import subprocess
import sys
from pathlib import Path

import pytest

from cpp_deps_checker import report
from cpp_deps_checker.cli import main
from cpp_deps_checker.report import Graph, Kind
from tests.harness import FIXTURES, configure


def test_violations_are_printed_and_fail(
    tmp_path: Path, capsys: pytest.CaptureFixture[str]
) -> None:
    build = configure(FIXTURES / "definition_order_late", tmp_path / "build")
    capsys.readouterr()

    assert main(["check", str(build.dir)]) == 1

    (line,) = capsys.readouterr().out.splitlines()
    assert line.startswith("CMakeLists.txt:5: error: a links late")
    assert line.endswith(" [definition-order]")


def test_clean_project_passes_silently(
    tmp_path: Path, capsys: pytest.CaptureFixture[str]
) -> None:
    build = configure(FIXTURES / "definition_order_imported", tmp_path / "build")
    capsys.readouterr()

    assert main(["check", str(build.dir)]) == 0
    assert capsys.readouterr().out == ""


def test_trace_option_overrides_default(tmp_path: Path) -> None:
    build = configure(FIXTURES / "definition_order_late", tmp_path / "build")
    moved = build.trace.rename(tmp_path / "elsewhere.json")

    assert main(["check", str(build.dir), "--trace", str(moved)]) == 1


def test_unloadable_input_is_reported(
    tmp_path: Path, capsys: pytest.CaptureFixture[str]
) -> None:
    assert main(["check", str(tmp_path)]) == 2
    assert "no File API reply" in capsys.readouterr().err


def test_console_script(tmp_path: Path) -> None:
    build = configure(FIXTURES / "definition_order_imported", tmp_path / "build")
    script = Path(sys.executable).parent / "cpp-deps-checker"

    assert subprocess.run([script, "check", build.dir], check=False).returncode == 0


def test_missing_default_compile_commands_warns_and_skips_includes(
    tmp_path: Path, capsys: pytest.CaptureFixture[str]
) -> None:
    build = configure(FIXTURES / "definition_order_imported", tmp_path / "build")
    capsys.readouterr()

    assert main(["check", str(build.dir)]) == 0
    assert "include rules skipped" in capsys.readouterr().err


def test_missing_explicit_compile_commands_fails(
    tmp_path: Path, capsys: pytest.CaptureFixture[str]
) -> None:
    build = configure(FIXTURES / "definition_order_imported", tmp_path / "build")
    missing = tmp_path / "nowhere.json"

    assert main(["check", str(build.dir), "--compile-commands", str(missing)]) == 2
    assert "nowhere.json" in capsys.readouterr().err


def test_include_violation_through_the_cli(
    tmp_path: Path, capsys: pytest.CaptureFixture[str]
) -> None:
    build = configure(FIXTURES / "direct_layering_transitive", tmp_path / "build")
    capsys.readouterr()

    assert main(["check", str(build.dir)]) == 1
    (line,) = capsys.readouterr().out.splitlines()
    assert line.startswith("a/a.cpp:2: error: ")


def test_failure_outside_header_set_verification_is_unusable_input(
    tmp_path: Path, capsys: pytest.CaptureFixture[str]
) -> None:
    build = configure(FIXTURES / "includes_broken_source", tmp_path / "build")
    capsys.readouterr()

    assert main(["check", str(build.dir)]) == 2
    assert "missing.hpp" in capsys.readouterr().err


def test_warnings_only_pass(tmp_path: Path, capsys: pytest.CaptureFixture[str]) -> None:
    build = configure(FIXTURES / "placement_above_consumers", tmp_path / "build")
    capsys.readouterr()

    assert main(["check", str(build.dir)]) == 0
    (line,) = capsys.readouterr().out.splitlines()
    assert line.startswith("lexer/CMakeLists.txt:1: warning: ")


def test_annotations_are_relative_to_the_working_directory(
    tmp_path: Path, capsys: pytest.CaptureFixture[str], monkeypatch: pytest.MonkeyPatch
) -> None:
    build = configure(FIXTURES / "definition_order_late", tmp_path / "build")
    capsys.readouterr()
    monkeypatch.chdir(FIXTURES)

    assert main(["check", str(build.dir), "--annotations"]) == 1

    (line,) = capsys.readouterr().out.splitlines()
    assert line.startswith(
        "::error file=definition_order_late/CMakeLists.txt,line=5,"
        "title=definition-order::a links late"
    )


def test_graph_is_written_without_checking(
    tmp_path: Path, capsys: pytest.CaptureFixture[str]
) -> None:
    build = configure(FIXTURES / "definition_order_late", tmp_path / "build")
    output = tmp_path / "graph.md"

    assert main(["graph", str(build.dir), "--output", str(output)]) == 0

    expected = Graph(
        nodes={"a": Kind.PRODUCTION, "base": Kind.PRODUCTION, "late": Kind.PRODUCTION},
        edges=frozenset({("a", "base"), ("a", "late")}),
    )
    assert report.parse_mermaid(output.read_text()) == expected
    capsys.readouterr()
    assert main(["graph", str(build.dir)]) == 0
    assert capsys.readouterr().out == output.read_text()


def test_graph_of_unloadable_input_fails(
    tmp_path: Path, capsys: pytest.CaptureFixture[str]
) -> None:
    assert main(["graph", str(tmp_path)]) == 2
    assert "no File API reply" in capsys.readouterr().err
