import subprocess
from dataclasses import dataclass
from pathlib import Path

from cpp_deps_checker.cli import context
from cpp_deps_checker.model import Location
from cpp_deps_checker.rules import Level, Violation, check

FIXTURES = Path(__file__).parent / "fixtures"
CONFIGURE = Path(__file__).parent.parent / "action" / "configure.sh"


@dataclass(frozen=True)
class Build:
    dir: Path
    reply_dir: Path
    index: Path
    trace: Path
    compile_commands: Path


def configure(source_dir: Path, build_dir: Path, *cmake_args: str) -> Build:
    """Configure source_dir with the inputs the Action collects from a consumer,
    through the Action's own script."""
    subprocess.run(
        [
            CONFIGURE,
            source_dir,
            build_dir,
            "-G",
            "Ninja",
            "-DCMAKE_CXX_COMPILER=clang++",
            *cmake_args,
        ],
        check=True,
    )
    api_dir = build_dir / ".cmake" / "api" / "v1"
    reply_dir = api_dir / "reply"
    # cmake-file-api(7): the current index is the one with the largest name.
    index = max(reply_dir.glob("index-*.json"))
    return Build(
        dir=build_dir,
        reply_dir=reply_dir,
        index=index,
        trace=build_dir / "trace.json",
        compile_commands=build_dir / "compile_commands.json",
    )


def check_fixture(fixture: str, build_dir: Path) -> list[Violation]:
    """Configure a fixture and run every rule on it, wired as the CLI does."""
    build = configure(FIXTURES / fixture, build_dir)
    commands = build.compile_commands if build.compile_commands.exists() else None
    return check(context(build.dir, build.trace, commands))


def summary(violations: list[Violation]) -> list[tuple[str, Level, Location | None]]:
    return [(v.rule, v.level, v.location) for v in violations]
