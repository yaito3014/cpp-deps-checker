# cpp-deps-checker

Checks that a CMake project's `target_link_libraries` graph is acyclic and defined bottom-up, and that header includes follow it. See [docs/design.md](docs/design.md).

## Usage

Configure the project with a File API query and a JSON trace, then check the build directory. Implemented so far: rule 1 (no cycles) and rule 2 (definition order).

```sh
mkdir -p build/.cmake/api/v1/query
touch build/.cmake/api/v1/query/codemodel-v2
cmake -S . -B build -DCMAKE_OPTIMIZE_DEPENDENCIES:BOOL=OFF \
  --trace-format=json-v1 --trace-expand --trace-redirect=build/trace.json
cpp-deps-checker check build
```

Exit status: 0 when clean (or warnings only), 1 when any error is found, 2 when the inputs cannot be loaded. `--trace FILE` points at a trace written elsewhere.

## Development

```sh
uv sync
uv run ruff format --check
uv run ruff check
uv run ty check
uv run pytest
```
