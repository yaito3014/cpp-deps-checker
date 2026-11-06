import json
from pathlib import Path

from tests.harness import FIXTURES, configure


def test_configure_produces_codemodel_trace_and_compile_commands(
    tmp_path: Path,
) -> None:
    build = configure(FIXTURES / "minimal", tmp_path / "build")

    index = json.loads(build.index.read_text())
    codemodel = index["reply"]["codemodel-v2"]
    assert codemodel["version"]["major"] == 2
    assert codemodel["version"]["minor"] >= 9
    assert (build.reply_dir / codemodel["jsonFile"]).is_file()

    trace_commands = [
        json.loads(line).get("cmd") for line in build.trace.read_text().splitlines()
    ]
    assert "add_library" in trace_commands

    (command,) = json.loads(build.compile_commands.read_text())
    assert "clang++" in command["command"]

    cache = (build.dir / "CMakeCache.txt").read_text()
    assert "CMAKE_OPTIMIZE_DEPENDENCIES:BOOL=OFF" in cache
