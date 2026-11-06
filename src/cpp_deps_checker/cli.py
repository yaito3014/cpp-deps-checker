import argparse
import sys
from collections.abc import Sequence
from pathlib import Path

from cpp_deps_checker import report
from cpp_deps_checker.model import ReplyError, load
from cpp_deps_checker.rules import Context, Level, check


def _parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(prog="cpp-deps-checker")
    commands = parser.add_subparsers(dest="command", required=True)
    check_command = commands.add_parser(
        "check", help="check a configured CMake build directory"
    )
    check_command.add_argument("build_dir", type=Path)
    check_command.add_argument(
        "--trace",
        type=Path,
        help="CMake JSON trace of the configure (default: BUILD_DIR/trace.json)",
    )
    return parser


def main(argv: Sequence[str] | None = None) -> int:
    """Exit status: 0 clean or warnings only, 1 errors found, 2 unusable input."""
    args = _parser().parse_args(argv)
    build_dir: Path = args.build_dir
    trace: Path = args.trace or build_dir / "trace.json"
    try:
        model = load(build_dir, trace)
    except (ReplyError, FileNotFoundError) as error:
        print(f"cpp-deps-checker: error: {error}", file=sys.stderr)
        return 2
    violations = check(Context(model=model))
    for violation in violations:
        print(report.text(violation))
    return 1 if any(v.level is Level.ERROR for v in violations) else 0
