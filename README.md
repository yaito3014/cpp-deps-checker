# cpp-deps-checker

Checks that a CMake project's `target_link_libraries` graph is acyclic and defined bottom-up, and that header includes follow it. See [docs/design.md](docs/design.md).

## Building

cpp-deps-checker is a C++23 command line built with CMake, its libraries from vcpkg (`VCPKG_ROOT` set) and LLVM 24, expected in `/usr/lib/llvm-24`. LLVM 24 is not released yet; apt.llvm.org's development snapshot has it.

```sh
cmake --preset default -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF
cmake --build --preset default --target cpp_deps_checker_main
```

The executable is `build/default/cli/cpp-deps-checker`. The clang-tidy and clangd plugin is the target `cpp_deps_checker_tidy`, built as `build/default/clang/tidy/libcpp-deps-checker-tidy.so`; it loads only into clang-tidy and clangd of the LLVM it was built with, and nothing checks this at load time.

## Usage

Configure the project with Clang, a File API query, a JSON trace, a compilation database, and interface header set verification, then check the build directory. Implemented so far: rule 1 (no cycles), rule 2 (definition order), rule 3 (direct layering), rule 3a (self-contained headers), rule 4 (entry point only), rule 5 (nesting is privacy), rule 6 (placement, a warning), and rule 7 (test isolation). By default, test targets are those defined in a `tests/` or `test/` directory at any depth, and the composition roots are the production targets nothing else links; `.cpp-deps-checker.yaml` in the top-level source directory changes both (see below). External targets (imported, or defined outside the source tree or in a build directory inside it, as FetchContent dependencies are) own no files and are exempt from rules 2, 3a, 5 and 6.

```sh
mkdir -p build/.cmake/api/v1/query
touch build/.cmake/api/v1/query/codemodel-v2
cmake -S . -B build -DCMAKE_CXX_COMPILER=clang++ \
  -DCMAKE_EXPORT_COMPILE_COMMANDS:BOOL=ON -DCMAKE_OPTIMIZE_DEPENDENCIES:BOOL=OFF \
  -DCMAKE_VERIFY_INTERFACE_HEADER_SETS:BOOL=ON \
  --trace-format=json-v1 --trace-expand --trace-redirect=build/trace.json
cpp-deps-checker check build
```

```yaml
# .cpp-deps-checker.yaml; every key is optional.
composition_roots: [app]  # target names
tests:
  directories: ["**/tests/**", "**/test/**"]  # globs of the defining CMakeLists.txt path
  names: ["*_test"]                           # target name patterns
```

[action/configure.sh](action/configure.sh) `SOURCE_DIR BUILD_DIR [CMAKE_ARG...]` runs the same configure; the Action and the tests use it.

Exit status: 0 when clean (or warnings only), 1 when any error is found, 2 when the inputs cannot be loaded, including a translation unit that fails to preprocess outside header set verification, when an output file cannot be written, or on a usage error. `--trace FILE` and `--compile-commands FILE` point at inputs written elsewhere. `--annotations` prints GitHub Actions `::error` and `::warning` workflow commands instead of text lines, with paths relative to the current directory (the repository root in CI). Without a compilation database at its default path, the include rules are skipped with a warning.

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

The runner must be a GitHub-hosted Ubuntu runner (it uses `sudo apt-get`) with CMake 4.2 or later, Clang, and whatever generator `cmake-args` names. The Action installs LLVM 24 from apt.llvm.org's development snapshot, builds the tool from its own checkout, and caches the build per tool version and LLVM snapshot; the snapshot changes often, so expect a build of a few minutes when it has. It annotates the violations, writes a job summary with every violation and the edges new since `base-ref` (default: the pull request's base branch, configured a second time in a git worktree, without submodules), and uploads the graph as the artifact `artifact-name` (default `cpp-deps-graph`). `cmake-args` is split on whitespace, without quoting.

## clang-tidy and clangd

The plugin adds rules 3 and 4 to clang-tidy and clangd 24 as the checks `cpp-deps-direct-layering` and `cpp-deps-entry-point`. Each translation unit is checked against the File API reply in its compile command's directory or the nearest parent that has one, and with the project's config file, both read again after a configure or a change to the config file, so the build directory needs only the File API query and `CMAKE_OPTIMIZE_DEPENDENCIES=OFF`. A directory without a usable reply or config gets one `clang-tidy-config` warning per configure or config change.

```sh
clang-tidy --load=build/default/clang/tidy/libcpp-deps-checker-tidy.so --checks='-*,cpp-deps-*' -p build src/a.cpp
```

With Ninja and C++20 or later, CMake scans for modules by default and names a module map in each compile command that only a build writes; clang-tidy fails on a translation unit whose module map is missing, so build first or configure with `-DCMAKE_CXX_SCAN_FOR_MODULES=OFF`. `check` and clangd ignore a missing module map.

Start clangd with `--load=/path/to/libcpp-deps-checker-tidy.so` and enable the checks in `.clangd`. clangd runs a check it does not know to be fast only with `FastCheckFilter: Loose`:

```yaml
Diagnostics:
  ClangTidy:
    Add: cpp-deps-*
    FastCheckFilter: Loose
```

clangd checks the include directives of the open file only, and does not show the warning about a missing reply.

## Development

```sh
cmake --preset default
cmake --build --preset default
ctest --preset default
clang-format --dry-run --Werror $(git ls-files '*.cpp' '*.hpp' ':!tests/fixtures')
uvx --from shellcheck-py shellcheck action/*.sh
uvx --from actionlint-py actionlint
```

The design and the reasons behind each rule are in [docs/design.md](docs/design.md). Each fixture under `tests/fixtures` that configures with the default arguments has an `expected.txt`: its exit status and the exact standard output of `check` on it, which a Catch2 test checks.

The workflows' action dependencies are locked in `.github/workflows/actions.lock`. After changing a `uses:` line in a workflow or in `action.yml`, regenerate it with `gh actions-lock` (the `github/gh-actions-lock` extension).
