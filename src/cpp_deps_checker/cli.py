import argparse
import sys
from collections.abc import Sequence
from pathlib import Path

from cpp_deps_checker import report
from cpp_deps_checker.classify import classify, find_tests
from cpp_deps_checker.includes import IncludeError, collect
from cpp_deps_checker.model import Model, ReplyError, load
from cpp_deps_checker.rules import Context, Level, check


def _parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(prog="cpp-deps-checker")
    commands = parser.add_subparsers(dest="command", required=True)
    check_command = commands.add_parser(
        "check", help="check a configured CMake build directory"
    )
    graph_command = commands.add_parser(
        "graph", help="write the target graph as a Mermaid flowchart in Markdown"
    )
    for command in (check_command, graph_command):
        command.add_argument("build_dir", type=Path)
        command.add_argument(
            "--trace",
            type=Path,
            help="CMake JSON trace of the configure (default: BUILD_DIR/trace.json)",
        )
    check_command.add_argument(
        "--compile-commands",
        type=Path,
        help="compilation database (default: BUILD_DIR/compile_commands.json)",
    )
    check_command.add_argument(
        "--annotations",
        action="store_true",
        help="print GitHub Actions workflow commands instead of text, with paths "
        "relative to the current directory",
    )
    graph_command.add_argument(
        "--output", "-o", type=Path, help="file to write (default: standard output)"
    )
    return parser


def _tests(model: Model) -> frozenset[str]:
    # The consumer config that will set the test patterns is not designed
    # yet (docs/design.md), so the defaults apply.
    return find_tests(model)


def context(build_dir: Path, trace: Path, compile_commands: Path | None) -> Context:
    """Every rule's input; without compile_commands there are no include edges.

    A translation unit that fails to preprocess is a violation if it verifies
    a header set, and unusable input otherwise.
    """
    model = load(build_dir, trace)
    owners = classify(model)
    tests = _tests(model)
    if compile_commands is None:
        return Context(
            model=model, includes=(), failures=(), owners=owners, tests=tests
        )
    collection = collect(
        compile_commands, keep=lambda path: owners.of(path) is not None
    )
    for failure in collection.failures:
        if model.verified_target(failure.source) is None:
            raise IncludeError(
                f"preprocessing {failure.source} failed:\n{failure.output}"
            )
    return Context(
        model=model,
        includes=collection.includes,
        failures=collection.failures,
        owners=owners,
        tests=tests,
    )


def _error(error: Exception) -> int:
    print(f"cpp-deps-checker: error: {error}", file=sys.stderr)
    return 2


def _check(args: argparse.Namespace) -> int:
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
        return _error(error)
    violations = check(ctx)
    root = Path.cwd().resolve()
    for violation in violations:
        print(
            report.annotation(violation, ctx.model.source_dir, root)
            if args.annotations
            else report.text(violation)
        )
    return 1 if any(v.level is Level.ERROR for v in violations) else 0


def _graph(args: argparse.Namespace) -> int:
    build_dir: Path = args.build_dir
    output: Path | None = args.output
    try:
        model = load(build_dir, args.trace or build_dir / "trace.json")
    except (ReplyError, FileNotFoundError) as error:
        return _error(error)
    text = report.mermaid(report.graph(model, _tests(model)))
    if output is None:
        sys.stdout.write(text)
    else:
        output.write_text(text)
    return 0


def main(argv: Sequence[str] | None = None) -> int:
    """Exit status: 0 clean or warnings only, 1 errors found, 2 unusable input."""
    args = _parser().parse_args(argv)
    return _graph(args) if args.command == "graph" else _check(args)
