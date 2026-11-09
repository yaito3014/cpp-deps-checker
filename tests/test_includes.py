import json
from pathlib import Path

import pytest

from cpp_deps_checker.includes import (
    Diagnostic,
    Directive,
    Include,
    collect,
    directive_lines,
    parse_directives,
    parse_tree,
)
from tests.harness import FIXTURES, configure


def test_parse_tree_links_each_header_to_the_one_above() -> None:
    output = (
        ". /p/a.hpp\n"
        ".. /p/c.hpp\n"
        ". /p/b.hpp\n"
        ".. /p/a.hpp\n"
        "Multiple include guards may be useful for:\n"
        "/p/x.h\n"
    )
    m, a, b, c = (Path(f"/p/{name}") for name in ("m.cpp", "a.hpp", "b.hpp", "c.hpp"))

    assert parse_tree(output, source=m, directory=Path("/build")) == [
        (m, a),
        (a, c),
        (m, b),
        (b, a),
    ]


def test_parse_tree_resolves_against_the_compile_directory() -> None:
    edges = parse_tree(". ./a.hpp\n", source=Path("m.cpp"), directory=Path("/build"))
    assert edges == [(Path("/build/m.cpp"), Path("/build/a.hpp"))]


_DI = " /* clang -E -dI */"


def test_parse_directives_tracks_lines_through_line_markers() -> None:
    output = "\n".join(
        [
            '# 1 "m.cpp"',
            '# 1 "<built-in>" 1',
            '# 1 "<command line>" 1',
            '#include "forced.hpp"' + _DI,
            '# 1 "./forced.hpp" 1',
            '# 2 "<built-in>" 2',
            '# 1 "m.cpp" 2',
            "",
            "",
            '#include "a.hpp"' + _DI,
            '# 3 "m.cpp"',
            '# 1 "inc/a.hpp" 1',
            "#include_next <a.hpp>" + _DI,
            "int x;",
            '# 4 "m.cpp" 2',
            "#pragma once",
            '#import "a.hpp"' + _DI,
            '# 1 "/usr/include/s\\\\x\\"y.h" 1 3 4',
            '# 6 "m.cpp" 2',
            '#include "b.hpp"' + _DI,
            "int y;",
            # Entered after other output, so not by the directive above.
            '# 1 "inc/c.hpp" 1',
            "",
        ]
    )
    m, a = Path("/build/m.cpp"), Path("/build/inc/a.hpp")

    assert parse_directives(output, directory=Path("/build")) == [
        Directive(m, 3, Path("/build/inc/a.hpp")),
        Directive(a, 1, None),
        Directive(m, 5, Path('/usr/include/s\\x"y.h')),
        Directive(m, 6, None),
    ]


_M, _A, _B = Path("/p/m.cpp"), Path("/p/a.hpp"), Path("/p/b.hpp")


def test_directive_lines_pair_edges_and_directives_in_order() -> None:
    edges = [(_M, _A), (_A, _B), (_M, _B)]
    directives = [Directive(_M, 1, _A), Directive(_A, 4, _B), Directive(_M, 2, None)]

    assert directive_lines(edges, directives, error=None) == [1, 4, 2]


@pytest.mark.parametrize(
    "directives",
    [
        [Directive(_M, 1, _A)],
        [Directive(_M, 1, _A), Directive(_M, 2, None), Directive(_M, 3, None)],
        [Directive(_M, 1, _A), Directive(_A, 2, None)],
        [Directive(_M, 1, _B), Directive(_M, 2, None)],
    ],
)
def test_directive_lines_are_unknown_when_the_outputs_disagree(
    directives: list[Directive],
) -> None:
    edges = [(_M, _A), (_M, _B)]

    assert directive_lines(edges, directives, error=None) == [None, None]


def test_directive_lines_skip_the_include_that_failed() -> None:
    edges = [(_M, _A), (_M, _B)]
    directives = [
        Directive(_M, 1, _A),
        Directive(_M, 2, None),
        Directive(_M, 3, _B),
    ]
    error = Diagnostic(_M, 2, "'missing.hpp' file not found")

    assert directive_lines(edges, directives, error) == [1, 3]


@pytest.fixture(scope="module")
def public_private(tmp_path_factory: pytest.TempPathFactory) -> Path:
    build_dir = tmp_path_factory.mktemp("build")
    return configure(FIXTURES / "direct_layering_public_private", build_dir).dir


def test_collect_runs_every_translation_unit(public_private: Path) -> None:
    root = (FIXTURES / "direct_layering_public_private").resolve()
    a_cpp, a_hpp = root / "a/a.cpp", root / "a/include/a/a.hpp"
    c_hpp = root / "a/c/include/c/c.hpp"
    build = public_private.resolve()
    verify_a = build / "a/a_verify_interface_header_sets/a/a.hpp.cxx"
    verify_c = build / "a/c/c_verify_interface_header_sets/c/c.hpp.cxx"

    collection = collect(public_private / "compile_commands.json", keep=lambda _: True)

    assert set(collection.includes) == {
        Include(a_cpp, a_hpp, 1),
        Include(a_hpp, c_hpp, 3),
        Include(verify_a, a_hpp, 2),
        Include(verify_c, c_hpp, 2),
    }
    # a links c PRIVATE, so a's verification lacks c's include directory.
    assert [failure.source for failure in collection.failures] == [verify_a]


def test_collect_keeps_only_requested_includers(public_private: Path) -> None:
    collection = collect(
        public_private / "compile_commands.json", keep=lambda p: p.suffix == ".cpp"
    )
    assert [include.includer.name for include in collection.includes] == ["a.cpp"]


def test_preprocessing_failure_is_returned_with_edges_so_far(tmp_path: Path) -> None:
    (tmp_path / "ok.hpp").write_text("")
    (tmp_path / "m.cpp").write_text('#include "ok.hpp"\n#include "missing.hpp"\n')
    commands = tmp_path / "compile_commands.json"
    commands.write_text(
        json.dumps(
            [
                {
                    "directory": str(tmp_path),
                    "arguments": ["clang++", "-c", "m.cpp", "-o", "m.o"],
                    "file": "m.cpp",
                }
            ]
        )
    )
    root = tmp_path.resolve()

    collection = collect(commands, keep=lambda _: True)

    assert collection.includes == (Include(root / "m.cpp", root / "ok.hpp", 1),)
    (failure,) = collection.failures
    assert failure.source == root / "m.cpp"
    assert failure.error == Diagnostic(
        root / "m.cpp", 2, "'missing.hpp' file not found"
    )
    assert "missing.hpp" in failure.output


def _commands(directory: Path, *sources: str) -> Path:
    commands = directory / "compile_commands.json"
    commands.write_text(
        json.dumps(
            [
                {
                    "directory": str(directory),
                    "arguments": ["clang++", "-Iinc", "-c", s, "-o", f"{s}.o"],
                    "file": s,
                }
                for s in sources
            ]
        )
    )
    return commands


def _guarded(name: str, body: str = "") -> str:
    guard = name.upper().replace("/", "_").replace(".", "_")
    return f"#ifndef {guard}\n#define {guard}\n{body}#endif\n"


def test_lines_come_from_the_directives_the_preprocessor_ran(tmp_path: Path) -> None:
    (tmp_path / "inc/sub").mkdir(parents=True)
    (tmp_path / "inc/a.hpp").write_text(_guarded("a.hpp"))
    (tmp_path / "inc/b.hpp").write_text(_guarded("b.hpp"))
    (tmp_path / "inc/sub/c.hpp").write_text(_guarded("c.hpp", '#include "../b.hpp"\n'))
    (tmp_path / "m.cpp").write_text(
        "/*\n"
        '#include "a.hpp"\n'  # 2: commented out
        "*/\n"
        "#if 0\n"
        '#include "a.hpp"\n'  # 5: not compiled
        "#endif\n"
        "#include \\\n"  # 7: continued on line 8
        '  "a.hpp"\n'
        '#include "sub/c.hpp"\n'  # 9
        '#define HEADER "b.hpp"\n'
        "#include HEADER\n"  # 11: skipped by the guard
    )
    root = tmp_path.resolve()
    m, a, b, c = (
        root / "m.cpp",
        root / "inc/a.hpp",
        root / "inc/b.hpp",
        root / "inc/sub/c.hpp",
    )

    collection = collect(_commands(tmp_path, "m.cpp"), keep=lambda _: True)

    assert collection.includes == tuple(
        sorted(
            [
                Include(m, a, 7),
                Include(m, c, 9),
                Include(c, b, 3),
                Include(m, b, 11),
            ]
        )
    )


def test_an_edge_keeps_its_line_from_another_translation_unit(
    tmp_path: Path,
) -> None:
    (tmp_path / "inc").mkdir()
    (tmp_path / "inc/a.hpp").write_text(_guarded("a.hpp", '#include "b.hpp"\n'))
    (tmp_path / "inc/b.hpp").write_text(_guarded("b.hpp"))
    # Two missing headers: only the first is reported, so bad.cpp's
    # directives and -H output disagree and its edges get no line.
    (tmp_path / "bad.cpp").write_text(
        '#include "a.hpp"\n#include "missing1.hpp"\n#include "missing2.hpp"\n'
    )
    (tmp_path / "good.cpp").write_text('#include "a.hpp"\n')
    root = tmp_path.resolve()
    a, b = root / "inc/a.hpp", root / "inc/b.hpp"

    collection = collect(
        _commands(tmp_path, "bad.cpp", "good.cpp"), keep=lambda _: True
    )

    assert collection.includes == tuple(
        sorted(
            [
                Include(root / "bad.cpp", a, None),
                Include(a, b, 3),
                Include(root / "good.cpp", a, 1),
            ]
        )
    )
