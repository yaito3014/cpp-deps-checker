"""Every fixture with an expected.txt, checked through a command line.

expected.txt holds `# exit status: N` and then the exact standard output of
`check` on the fixture, configured as the Action configures it. The command is
CPP_DEPS_CHECKER, split like a shell would, when set, so that another
implementation can be checked against the same files; otherwise it is this
package's cpp-deps-checker.
"""

import os
import shlex
import shutil
import subprocess
from pathlib import Path

import pytest

from tests.harness import FIXTURES, configure

_STATUS = "# exit status: "


def _command() -> list[str]:
    if command := os.environ.get("CPP_DEPS_CHECKER"):
        return shlex.split(command)
    found = shutil.which("cpp-deps-checker")
    assert found is not None, "cpp-deps-checker is not on PATH; run under uv run"
    return [found]


@pytest.mark.parametrize(
    "fixture",
    sorted(p.parent.name for p in FIXTURES.glob("*/expected.txt")),
)
def test_check_output(fixture: str, tmp_path: Path) -> None:
    status_line, _, stdout = (
        (FIXTURES / fixture / "expected.txt").read_text().partition("\n")
    )
    assert status_line.startswith(_STATUS)
    build = configure(FIXTURES / fixture, tmp_path / "build")

    result = subprocess.run(
        [*_command(), "check", build.dir],
        capture_output=True,
        text=True,
        check=False,
    )

    assert (result.returncode, result.stdout) == (
        int(status_line.removeprefix(_STATUS)),
        stdout,
    ), result.stderr


def test_every_fixture_that_configures_alone_has_expected_output() -> None:
    # The Action's fixtures need arguments or a failing configure on purpose.
    missing = {p.name for p in FIXTURES.iterdir() if not (p / "expected.txt").exists()}

    assert missing == {"action_base", "action_broken"}
