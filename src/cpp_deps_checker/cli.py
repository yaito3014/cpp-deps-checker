import argparse
import sys
from collections.abc import Sequence
from pathlib import Path

from cpp_deps_checker import report
from cpp_deps_checker.classify import classify
from cpp_deps_checker.includes import IncludeError, collect
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
    check_command.add_argument(
        "--compile-commands",
        type=Path,
        help="compilation database (default: BUILD_DIR/compile_commands.json)",
    )
    return parser


def context(build_dir: Path, trace: Path, compile_commands: Path | None) -> Context:
    """Every rule's input; without compile_commands there are no include edges."""
    model = load(build_dir, trace)
    owners = classify(model)
    includes = (
        ()
        if compile_commands is None
        else collect(compile_commands, keep=lambda path: owners.of(path) is not None)
    )
    return Context(model=model, includes=includes, owners=owners)


def main(argv: Sequence[str] | None = None) -> int:
    """Exit status: 0 clean or warnings only, 1 errors found, 2 unusable input."""
    args = _parser().parse_args(argv)
    build_dir: Path = args.build_dir
    trace: Path = args.trace or build_dir / "trace.json"
    compile_commands: Path | None = args.compile_commands
    if compile_commands is None:
        default = build_dir / "compile_commands.json"
        if default.exists():
            compile_commands = default
        else:
            print(
                f"cpp-deps-checker: warning: no {default}; include rules skipped "
                "(configure with CMAKE_EXPORT_COMPILE_COMMANDS=ON)",
                file=sys.stderr,
            )
    try:
        ctx = context(build_dir, trace, compile_commands)
    except (ReplyError, IncludeError, FileNotFoundError) as error:
        print(f"cpp-deps-checker: error: {error}", file=sys.stderr)
        return 2
    violations = check(ctx)
    for violation in violations:
        print(report.text(violation))
    return 1 if any(v.level is Level.ERROR for v in violations) else 0
