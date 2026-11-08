import os
import shutil
import subprocess
import sys
from pathlib import Path

import pytest

from tests.harness import FIXTURES

RUN = Path(__file__).parent.parent / "action" / "run.sh"
# Isolated from the user's git configuration (hooks, signing, identity).
_GIT_ENV = {
    "GIT_CONFIG_GLOBAL": os.devnull,
    "GIT_CONFIG_NOSYSTEM": "1",
    "GIT_AUTHOR_NAME": "test",
    "GIT_AUTHOR_EMAIL": "test@example.com",
    "GIT_COMMITTER_NAME": "test",
    "GIT_COMMITTER_EMAIL": "test@example.com",
}


def _git(cwd: Path, *args: str) -> str:
    return subprocess.run(
        ["git", *args],
        cwd=cwd,
        env={**os.environ, **_GIT_ENV},
        check=True,
        capture_output=True,
        text=True,
    ).stdout


def _checkout(tmp_path: Path, base: str, head: str, *, shallow: bool) -> Path:
    """A clone, as actions/checkout makes one, of a branch that replaces the
    project under proj/ on main with the head fixture."""
    origin = tmp_path / "origin.git"
    _git(tmp_path, "init", "--quiet", "--bare", "--initial-branch=main", str(origin))
    author = tmp_path / "author"
    _git(tmp_path, "clone", "--quiet", str(origin), str(author))
    shutil.copytree(FIXTURES / base, author / "proj")
    _git(author, "add", "--all")
    _git(author, "commit", "--quiet", "--message=base")
    _git(author, "push", "--quiet", "origin", "HEAD:main")
    shutil.rmtree(author / "proj")
    shutil.copytree(FIXTURES / head, author / "proj")
    _git(author, "add", "--all")
    _git(author, "commit", "--quiet", "--message=head")
    _git(author, "push", "--quiet", "origin", "HEAD:feature")
    checkout = tmp_path / "checkout"
    depth = ["--depth=1"] if shallow else []
    _git(
        tmp_path,
        "clone",
        "--quiet",
        *depth,
        "--branch=feature",
        f"file://{origin}",
        str(checkout),
    )
    return checkout


def _run(checkout: Path, work: Path, base_ref: str) -> subprocess.CompletedProcess[str]:
    tool = Path(sys.executable).parent  # holds the cpp-deps-checker script
    env = {
        **os.environ,
        **_GIT_ENV,
        "PATH": f"{tool}{os.pathsep}{os.environ['PATH']}",
        "SOURCE_DIR": "proj",
        "CMAKE_ARGS": "-G Ninja -DCMAKE_CXX_COMPILER=clang++ -DACTION_ARGS_PASSED=ON",
        "BASE_REF": base_ref,
        "WORK_DIR": str(work),
        "SUMMARY_FILE": str(work.parent / "summary.md"),
    }
    return subprocess.run(
        [RUN], cwd=checkout, env=env, capture_output=True, text=True, check=False
    )


@pytest.mark.parametrize("shallow", [True, False])
def test_new_edges_against_the_base_branch(tmp_path: Path, shallow: bool) -> None:
    checkout = _checkout(
        tmp_path, "action_base", "definition_order_late", shallow=shallow
    )
    work = tmp_path / "work"

    result = _run(checkout, work, "main")

    assert result.returncode == 1, result.stderr
    errors = [
        line for line in result.stdout.splitlines() if line.startswith("::error ")
    ]
    assert errors == [
        "::error file=proj/CMakeLists.txt,line=5,title=definition-order"
        "::a links late, which is defined later at late/CMakeLists.txt:1; "
        "define late before a"
    ]
    assert (work / "graph.md").read_text().startswith("```mermaid\n")
    summary = (tmp_path / "summary.md").read_text()
    assert summary.endswith("### New edges\n\n- `a` -> `late`\n")
    assert _git(checkout, "worktree", "list", "--porcelain").count("worktree ") == 1
    is_shallow = _git(checkout, "rev-parse", "--is-shallow-repository")
    assert is_shallow == f"{str(shallow).lower()}\n"


def test_base_that_does_not_configure_skips_new_edges(tmp_path: Path) -> None:
    checkout = _checkout(
        tmp_path, "action_broken", "definition_order_imported", shallow=True
    )

    result = _run(checkout, tmp_path / "work", "main")

    assert result.returncode == 0, result.stderr
    assert (
        "::warning title=cpp-deps-checker::could not configure main; "
        "new edges are not listed"
    ) in result.stdout.splitlines()
    summary = (tmp_path / "summary.md").read_text()
    assert summary.endswith("new edges are not listed.\n")


def test_no_base_ref_skips_new_edges(tmp_path: Path) -> None:
    checkout = _checkout(
        tmp_path, "action_base", "definition_order_imported", shallow=True
    )

    result = _run(checkout, tmp_path / "work", "")

    assert result.returncode == 0, result.stderr
    assert "::warning" not in result.stdout
    summary = (tmp_path / "summary.md").read_text()
    assert summary.endswith("new edges are not listed.\n")
