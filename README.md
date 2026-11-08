# cpp-deps-checker

Checks that a CMake project's `target_link_libraries` graph is acyclic and defined bottom-up, and that header includes follow it. See [docs/design.md](docs/design.md).

## Usage

Configure the project with Clang, a File API query, a JSON trace, a compilation database, and interface header set verification, then check the build directory. Implemented so far: rule 1 (no cycles), rule 2 (definition order), rule 3 (direct layering), rule 3a (self-contained headers), rule 4 (entry point only), and rule 7 (test isolation). Test targets are those defined under the top-level `tests/` or `test/` directory; the patterns are not configurable yet.

```sh
mkdir -p build/.cmake/api/v1/query
touch build/.cmake/api/v1/query/codemodel-v2
cmake -S . -B build -DCMAKE_CXX_COMPILER=clang++ \
  -DCMAKE_EXPORT_COMPILE_COMMANDS:BOOL=ON -DCMAKE_OPTIMIZE_DEPENDENCIES:BOOL=OFF \
  -DCMAKE_VERIFY_INTERFACE_HEADER_SETS:BOOL=ON \
  --trace-format=json-v1 --trace-expand --trace-redirect=build/trace.json
cpp-deps-checker check build
```

Exit status: 0 when clean (or warnings only), 1 when any error is found, 2 when the inputs cannot be loaded, including a translation unit that fails to preprocess outside header set verification. `--trace FILE` and `--compile-commands FILE` point at inputs written elsewhere. Without a compilation database at its default path, the include rules are skipped with a warning.

## Development

```sh
uv sync
uv run ruff format --check
uv run ruff check
uv run ty check
uv run pytest
```
