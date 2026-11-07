import json
from pathlib import Path

import pytest

from cpp_deps_checker.includes import (
    Include,
    IncludeError,
    collect,
    directive_line,
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

    assert parse_tree(output, source=m, directory=Path("/build")) == {
        (m, a),
        (a, c),
        (m, b),
        (b, a),
    }


def test_parse_tree_resolves_against_the_compile_directory() -> None:
    edges = parse_tree(". ./a.hpp\n", source=Path("m.cpp"), directory=Path("/build"))
    assert edges == {(Path("/build/m.cpp"), Path("/build/a.hpp"))}


def test_directive_line_prefers_the_longest_spelled_suffix() -> None:
    text = '#include <vector>\n#  include "a/detail/x.hpp"\n#include "x.hpp"\n'

    assert directive_line(text, Path("/p/a/detail/x.hpp")) == 2
    assert directive_line(text, Path("/p/other/x.hpp")) == 3
    assert directive_line(text, Path("/p/y.hpp")) is None


def test_directive_line_accepts_include_next() -> None:
    assert directive_line("#include_next <x.hpp>\n", Path("/p/x.hpp")) == 1


@pytest.fixture(scope="module")
def public_private(tmp_path_factory: pytest.TempPathFactory) -> Path:
    build_dir = tmp_path_factory.mktemp("build")
    return configure(FIXTURES / "direct_layering_public_private", build_dir).dir


def test_collect_runs_every_translation_unit(public_private: Path) -> None:
    root = (FIXTURES / "direct_layering_public_private").resolve()
    a_cpp, a_hpp = root / "a/a.cpp", root / "a/include/a/a.hpp"

    includes = collect(public_private / "compile_commands.json", keep=lambda _: True)

    assert set(includes) == {
        Include(a_cpp, a_hpp, 1),
        Include(a_hpp, root / "c/include/c/c.hpp", 3),
    }


def test_collect_keeps_only_requested_includers(public_private: Path) -> None:
    includes = collect(
        public_private / "compile_commands.json", keep=lambda p: p.suffix == ".cpp"
    )
    assert [include.includer.name for include in includes] == ["a.cpp"]


def test_preprocessing_failure_raises(tmp_path: Path) -> None:
    (tmp_path / "m.cpp").write_text('#include "missing.hpp"\n')
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

    with pytest.raises(IncludeError, match=r"missing\.hpp"):
        collect(commands, keep=lambda _: True)
