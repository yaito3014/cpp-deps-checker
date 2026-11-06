import subprocess
import sys
from pathlib import Path

import pytest

from cpp_deps_checker.cli import main
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
