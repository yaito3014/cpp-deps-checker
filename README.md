# cpp-deps-checker

Checks that a CMake project's `target_link_libraries` graph is acyclic and defined bottom-up, and that header includes follow it. See [docs/design.md](docs/design.md).

## Usage

Configure the project with Clang, a File API query, a JSON trace, a compilation database, and interface header set verification, then check the build directory. Implemented so far: rule 1 (no cycles), rule 2 (definition order), rule 3 (direct layering), rule 3a (self-contained headers), rule 4 (entry point only), rule 5 (nesting is privacy), rule 6 (placement, a warning), and rule 7 (test isolation). Test targets are those defined under the top-level `tests/` or `test/` directory, and the composition roots are the production targets nothing else links; neither is configurable yet. External targets (imported, or defined outside the source tree or in a build directory inside it, as FetchContent dependencies are) own no files and are exempt from rules 2, 3a, 5 and 6.

```sh
mkdir -p build/.cmake/api/v1/query
touch build/.cmake/api/v1/query/codemodel-v2
cmake -S . -B build -DCMAKE_CXX_COMPILER=clang++ \
  -DCMAKE_EXPORT_COMPILE_COMMANDS:BOOL=ON -DCMAKE_OPTIMIZE_DEPENDENCIES:BOOL=OFF \
  -DCMAKE_VERIFY_INTERFACE_HEADER_SETS:BOOL=ON \
  --trace-format=json-v1 --trace-expand --trace-redirect=build/trace.json
cpp-deps-checker check build
```

[action/configure.sh](action/configure.sh) `SOURCE_DIR BUILD_DIR [CMAKE_ARG...]` runs the same configure; the Action and the tests use it.

Exit status: 0 when clean (or warnings only), 1 when any error is found, 2 when the inputs cannot be loaded, including a translation unit that fails to preprocess outside header set verification. `--trace FILE` and `--compile-commands FILE` point at inputs written elsewhere. `--annotations` prints GitHub Actions `::error` and `::warning` workflow commands instead of text lines, with paths relative to the current directory (the repository root in CI). Without a compilation database at its default path, the include rules are skipped with a warning.

`cpp-deps-checker graph build` writes the declared edges between targets as a Mermaid flowchart in Markdown (`--output FILE` instead of standard output); it reads only the File API reply and the trace.

`check --summary FILE` appends a Markdown summary of the violations to FILE (in GitHub Actions, `$GITHUB_STEP_SUMMARY`), and with `--baseline GRAPH`, a graph written by `graph` on the base branch, it also lists the edges that graph lacks.

## GitHub Action

```yaml
on: pull_request
jobs:
  deps:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v7
      - uses: yaito3014/cpp-deps-checker@main
        with:
          source-dir: .  # CMake source directory, relative to the repository root
          cmake-args: -G Ninja -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++
```

The runner needs CMake 4.2 or later, Clang, and whatever generator `cmake-args` names; the Action installs only uv, Python if the runner lacks 3.14, and the tool. It annotates the violations, writes a job summary with every violation and the edges new since `base-ref` (default: the pull request's base branch, configured a second time in a git worktree, without submodules), and uploads the graph as the artifact `artifact-name` (default `cpp-deps-graph`). `cmake-args` is split on whitespace, without quoting.

## Development

```sh
uv sync
uv run ruff format --check
uv run ruff check
uv run ty check
uv run pytest
uvx --from shellcheck-py shellcheck action/*.sh
uvx --from actionlint-py actionlint
```

The workflows' action dependencies are locked in `.github/workflows/actions.lock`. After changing a `uses:` line in a workflow or in `action.yml`, regenerate it with `gh actions-lock` (the `github/gh-actions-lock` extension).
