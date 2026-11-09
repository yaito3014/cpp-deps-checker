# cpp-deps-checker design

## Purpose and scope

cpp-deps-checker (codename) is a standalone CI tool that enforces a layering policy on CMake projects. The `target_link_libraries` graph is the manifest; the tool checks that it is acyclic, defined bottom-up, and that header includes follow it. The rules are listed under Rules and severity.

In scope: multi-target CMake projects on CMake 4.2 or later, built with a Clang toolchain available in CI.

Out of scope: the order of sections within a single-header library (lowest level first), use of the `detail` namespace (needs the AST), and third-party vulnerability scanning.

## Rules and severity

Eight rules fail the check, one only warns, and two produce output. Every violation carries a `file:line` from a File API backtrace or the trace, and each rule is a pure function from the rule context (see Module structure) to a list of violations.

| # | Rule | What is checked | Level |
| --- | --- | --- | --- |
| 1 | No cycles | SCC search over declared edges (defined below). One violation per strongly connected component, with the shortest cycle through its first member in the message | error |
| 2 | Definition order | For every edge T1 -> T2, the `add_library`/`add_executable` of T2 runs before that of T1, by trace order. External targets (defined below) are exempt, on either side of the edge | error |
| 3 | Direct layering | A file owned by T1 (source or header) that includes a header owned by T2 != T1 requires T2 in `compileDependencies(T1)` if the includer is a source or private header, and in `interfaceCompileDependencies(T1)` if it is a public header. INTERFACE libraries have only public headers. Includes of external targets' headers will need a declared link too, once their owner is decided (see Open decisions) | error |
| 3a | Self-contained headers | Every header in a target's interface header sets preprocesses on its own with that target's usage requirements: its `CMAKE_VERIFY_INTERFACE_HEADER_SETS` translation unit passes the Clang `-E` pass. Reported at the compiler's first error. Only preprocessing is checked; building the verification targets catches the rest. External targets are exempt | error |
| 4 | Entry point only | T2's `detail/` headers are included only by T2's own files and by test targets. T2's private headers are included only by T2's own files | error |
| 5 | Nesting is privacy | A target nested under a parent directory (defined below) is consumed only by targets defined in that directory or below it. An outside consumer fails at its link with a "hoist: private detail is becoming shared API" message | error |
| 6 | Placement | A target placed above the lowest common ancestor of its consumers: over the production graph, rooted at the composition roots, T's immediate dominator D is that ancestor, and T must be defined in D's directory or below it. Reported at T's definition. A target whose only dominator is the virtual root above several composition roots has no placement | warning |
| 7 | Test isolation | No non-test target links a test target | error |
| 8 | Graph | `cpp-deps-checker graph` writes the declared edges between targets, by name, as a Mermaid flowchart in a Markdown file (rendered by GitHub). Header set verification targets and UTILITY targets (which cannot be linked) are left out; test and external targets are styled apart | output |
| 9 | New edges | Edges of the graph (rule 8) that the base branch's graph lacks, listed in the job summary after a table of every violation | output |

Definitions used by the rules:

- **Declared edges.** T1 -> T2 is a declared edge if T2's id appears in any of T1's `linkLibraries`, `compileDependencies`, `interfaceLinkLibraries`, or `interfaceCompileDependencies`, excluding entries that carry `fromDependency`. This matches what T1's `target_link_libraries` calls declare: edges out of INTERFACE libraries appear only in the two interface fields, `$<COMPILE_ONLY:...>` items only in the compile fields, and `$<LINK_ONLY:...>` items only in the link fields. Entries with `fromDependency` were injected by another target's `INTERFACE_LINK_LIBRARIES_DIRECT` and are not declared by T1. Non-target entries (`fragment`) are not edges.
- **File owner.** The owner of a file is decided in this order: (1) a target that lists the file, as a source or a file set member; (2) a target with a file set that lists no files (an abstract INTERFACE library, see Verified separately) whose base directory contains the file; (3) the target whose source directory contains the file. Within (2) and (3) the deepest directory wins. A file claimed by several targets at the same step has no owner, and so does a file no step claims. External targets (an open decision), UTILITY targets, and header set verification targets own nothing. A file that a non-imported external target lists, or that lies under its source directory or one of its file-less file sets' base directories, has no owner, even inside a directory a project target claims (a build directory in the source tree); an imported target's source directory is only where it is declared, so it hides nothing. Likewise, a file under a build directory inside the source tree is owned only by a target that lists it or by a file set base directory inside the build directory, so a generated header set verification source does not belong to a target whose base or source directory holds the build directory.
- **Public and private.** A file owned by an INTERFACE library is public. Otherwise a file is public if it is in a `PUBLIC` or `INTERFACE` file set, even if it is also in a `PRIVATE` one (CMake allows a file in several file sets of one target), and private if it is only in `PRIVATE` file sets, a loose source, or claimed only through the source directory.
- **Detail header.** A public file with a `detail` directory component below the directory that gave it its owner: the file set base directory that contains it (the deepest, if several do), or the owner's source directory for a loose source or a file claimed through it. A `detail/` above that directory does not count, so a target defined inside some `detail/` directory does not make all its headers detail headers. A private file is never a detail header. A file in several public file sets is a detail header only if it is one below each of their base directories, since any other base directory makes it reachable as an entry point.
- **Parent.** A target's parent directory is the nearest proper ancestor of its source directory that defines a production target other than a UTILITY target. Targets in one directory share one subtree, so the parent is a directory rather than a target, which works the same when a directory defines several targets. A parent at the top-level source directory constrains nothing.
- **External target.** A target whose code is not the project's: an imported target, or one defined outside the top-level source directory or inside a build directory nested in it, which is where FetchContent puts fetched sources. A dependency added with `add_subdirectory` from inside the source tree, and anything fetched by an in-source build (build directory equal to the source directory), are not recognized. `FetchContent_MakeAvailable()` turns header set verification off while it runs (cmake-variables(7), `CMAKE_VERIFY_INTERFACE_HEADER_SETS`), so fetched targets have no verification targets; an out-of-tree `add_subdirectory` can still bring some in, hence the rule 3a exemption.
- **Production target.** A target that is not external, not a test target, and not a header set verification target.
- **Production graph.** The declared edges between production targets. Until the consumer config names the composition roots, every production target that no other production target links is one.
- **Exceptions.** A rule may be broken only through an entry in the consumer config, and every entry must carry a `reason`.

## Inputs

The model is built from three inputs: the CMake File API for targets and edges, a CMake JSON trace for definition order, and a Clang `-H` pass for include edges.

**CMake File API, codemodel v2.9 or later (CMake 4.2+).** Read these target fields: `linkLibraries`, `compileDependencies`, `interfaceLinkLibraries`, and `interfaceCompileDependencies` (together the declared edges; the two compile fields also give header visibility for rule 3), `fileSets`, `sources`, `paths.source`, and `backtraceGraph`. Read both `targets` and `abstractTargets`, because imported targets and INTERFACE libraries without sources or file sets are reported in the latter. Configure with `CMAKE_OPTIMIZE_DEPENDENCIES=OFF`, which otherwise strips dependency information from the reply. Do not use the `dependencies` field: it mixes direct and transitive edges and omits INTERFACE targets.

The cmake-file-api(7) manual establishes, for the four dependency fields:

- `linkLibraries` holds the target's `LINK_LIBRARIES` evaluated non-transitively, without `$<COMPILE_ONLY:...>` items, plus entries injected by other targets' `INTERFACE_LINK_LIBRARIES_DIRECT`, which carry `fromDependency`.
- `compileDependencies` holds the same relationships without `$<LINK_ONLY:...>` items, with the same `fromDependency` marking.
- `interfaceLinkLibraries` and `interfaceCompileDependencies` come from `INTERFACE_LINK_LIBRARIES`; the latter omits `$<LINK_ONLY:...>` items.
- Each entry has exactly one of `id` (a target) or `fragment` (raw linker arguments); the compile fields have only `id`.

Reply index selection, from the same manual: since CMake 4.1 a failed configure writes an `error-*.json` index and keeps the older `index-*.json`. The current index is the one with the largest name after stripping the `index-` or `error-` prefix. If it is an error index, the reply is stale and the loader must fail rather than read the older `index-*.json`.

**CMake trace.** Run `cmake --trace-format=json-v1 --trace-expand --trace-redirect=<file>` and take the execution order of `add_library` and `add_executable` calls. The File API cannot supply this order: a target's backtrace starts at its own `CMakeLists.txt` with no `add_subdirectory` frame, and directory objects carry no backtrace. The trace also holds every try_compile project's configure, and a `try_compile(PROJECT ... SOURCE_DIR ...)` project is traced at its own source directory, not under `CMakeFiles/CMakeScratch`, with no field that tells it apart (verified on CMake 4.4.5). So a trace entry counts only if it matches a target's definition site from the File API: the file and line of the target's `backtrace` and the target name, compared with the entry's `file`, `line`, and first argument. `--trace-expand` is required so that a target created through a wrapper function (`add_library(${name} ...)`) carries its expanded name, and command names are compared case-insensitively because the trace keeps their spelling.

**Include edges.** For each TU in `compile_commands.json`, the tool runs the compile command (minus `-c` and `-o`) with `-E -dI -H -fshow-skipped-includes`, rebuilds the include tree from `-H`, and takes its parent -> child edges. Without `-fshow-skipped-includes`, a header blocked by its include guard on a later include is not printed and that edge is lost. Only edges whose includer has an owner are kept. `-H` gives no line numbers, so the line of each edge comes from the preprocessed output, where `-dI` keeps every include directive the preprocessor ran and line markers give its file and line. The directives and the `-H` entries are paired in order, each pair checked for the same includer and, when the header was entered, the same header; the directive at the TU's first error is dropped, since `-H` prints nothing for a header not found. Line markers carry the line numbers that `#line` sets, which are not where the directive is, so an includer whose text contains `#line` (or its GNU form `# 12 "file"`) anywhere, even in a comment or an `#if 0` block, gives its edges no line, and they are paired without comparing includers since `#line` may also rename the file. If the two still disagree, that TU's edges get no line rather than a wrong one, and an edge keeps the line another TU found for it. Since `-H` leaves out a `-include` header and everything under it, those edges come from the line markers instead: the header counts as included by the TU's main file, without a line, and each header entered below it is included by the file holding the directive. A header skipped there by its include guard is named by neither output, so that edge is missing. Reading the includer's text instead got commented-out, `#if 0`, continued, macro-spelled, and `../` includes wrong or lineless. A header that no TU includes contributes no edges. Configuring with `CMAKE_VERIFY_INTERFACE_HEADER_SETS=ON` gives every interface header set member a TU of its own, so a header-only library is checked without a consumer. A failing header set verification TU is a rule 3a violation, and the edges it printed before failing are kept. The TUs of external targets are not preprocessed: no rule uses their edges, and they may need headers that only a build generates. Any other TU that fails to preprocess stops the check (exit status 2), because its include edges are incomplete. Rejected alternatives: a textual `#include` scanner (sees `#if`'d-out branches, so false positives on platform code), and Clang module maps with `-fmodules-strict-decluse` (Clang-only enforcement that cannot feed the graph output).

### Verified on a fixture (CMake 4.4.4, codemodel 2.11)

Fixture: `a` links `PRIVATE b PUBLIC c PRIVATE e`; `b` is an INTERFACE library linking `d`; `e` is defined in a later `add_subdirectory` than `a`.

| Field or input | Result for `a` | Consequence |
| --- | --- | --- |
| `linkLibraries` | `[b, c, e]`, no `d` | Direct edges only, no transitive `d` |
| `interfaceLinkLibraries` | `[b, c, e]` | Includes PRIVATE deps for a static library, as `$<LINK_ONLY:...>`: unusable for visibility |
| `interfaceCompileDependencies` | `[c]` | Exactly the PUBLIC set: use for rule 3 |
| `dependencies` | `[e, c]` | Omits INTERFACE `b`: unusable |
| `abstractTargets` | holds `b`, `d` | Loader must read it |
| Trace | `add_library(a)` at `CMakeLists.txt:6`, `add_library(e)` at `late/CMakeLists.txt:1` after it | Rule 2 violation is detectable |

Re-checked on CMake 4.4.5 with the same fixture (`tests/fixtures/model_basics`):

- `a`'s `INTERFACE_LINK_LIBRARIES` property is `$<LINK_ONLY:b>;c;$<LINK_ONLY:e>`, which explains the `interfaceLinkLibraries` row.
- `a`'s `compileDependencies` is `[b, c, e]`.
- INTERFACE library `b` has no `linkLibraries` or `compileDependencies`; its edge to `d` appears only in `interfaceLinkLibraries` and `interfaceCompileDependencies`, both `[d]`. An edge set built from `linkLibraries` alone misses every edge out of an INTERFACE library, hence the declared-edges definition under Rules.

### Verified separately (CMake 4.4.5, codemodel 2.11; Clang 23.1.6)

**Imported targets carry no include directories in the reply.** Fixture: `ext::x` is an `INTERFACE IMPORTED` library with `INTERFACE_INCLUDE_DIRECTORIES` set, and `a` links it PRIVATE. The `ext::x` object (in `abstractTargets`) has `imported: true` and no include or file-set fields. The include directory appears only in `a`'s `compileGroups[].includes`, with the backtrace of the `target_link_libraries` call, the same backtrace as the `linkLibraries` entry. Attributing an include directory to one imported target by backtrace is therefore ambiguous when one call links several imported targets. How rule 3 finds the owner of an imported target's header is an open decision.

**Abstract INTERFACE libraries list no files.** An INTERFACE library with a `HEADERS` file set and no compiled sources is in `abstractTargets` with `sources: []`; its file set has only `baseDirectories`. A built target lists its file set members in `sources` with `fileSetIndexes` (codemodel 2.11; `fileSetIndex`, deprecated in 4.4, before that).

**No languages, no compilation database.** A project with `project(... LANGUAGES NONE)` gets no `compile_commands.json` even with `CMAKE_EXPORT_COMPILE_COMMANDS=ON`.

**Header set verification targets.** With `CMAKE_VERIFY_INTERFACE_HEADER_SETS=ON`, CMake adds an `OBJECT_LIBRARY` named `<t>_verify_interface_header_sets` for every target `t` (seen for STATIC and INTERFACE libraries, with or without header sets; executables not checked), plus the UTILITY targets `all_verify_header_sets` and `all_verify_interface_header_sets`. The verification target's only `linkLibraries` entry is `t`, its `paths.source` is `t`'s, its sources are one generated `<header>.cxx` per interface header set member in the build directory (containing `#include <spelled/header.hpp>` on line 2), and its backtrace node has a file but no line, so it has no definition site. The model recognizes it by name, single link, and missing definition. When `t` links a dependency PRIVATE, the verification TU lacks the dependency's include directory, so a public header that includes it fails there as well as violating rule 3. `CMAKE_VERIFY_PRIVATE_HEADER_SETS` (CMake 4.3) exists but is not used.

**Targets never link themselves.** CMake rejects a non-INTERFACE target that links itself and drops an INTERFACE library's self-link from the reply, so every cycle spans at least two targets.

**`-dI` keeps only the directives the preprocessor ran.** A directive in a comment or an `#if 0` block is not printed; a continued one is printed at its first line; `#include MACRO` is printed with the macro expanded; a header skipped by its guard, `#pragma once`, or a repeated `#import` is printed without a line marker entering it, and `-H` (with `-fshow-skipped-includes`) prints it too, in the same order. A header not found is printed but not listed by `-H`, and preprocessing goes on. A `-include` header is printed in `<built-in>`, but `-H` lists neither it nor the headers it includes.

**`-fshow-skipped-includes` prints guarded re-includes.** Fixture: `m.cpp` includes `a.hpp` then `b.hpp`, and `b.hpp` includes the already guarded `a.hpp`. Plain `-H` prints `. ./a.hpp` and `. ./b.hpp` only; with the flag it also prints `.. ./a.hpp` under `b.hpp`, so the `b.hpp -> a.hpp` edge is kept.

## Test targets

Test targets are exempt from the privacy and placement rules (5 and 6) and may include `detail/` headers, but every other rule still applies to them. Tests usually live in a separate directory, and many build tools expect that, so treating them as ordinary consumers would make rule 5 fail on almost every project.

**Identification.** The consumer config lists directory globs and name patterns, defaulting to `tests/**` and `test/**` with no name patterns. A target is a test target, including a test helper library, if the path of the `CMakeLists.txt` that defines it, relative to the top-level source directory, matches a directory glob (`pathlib.PurePath.full_match`, where `**` spans any number of segments), or if its name matches a name pattern (`fnmatch`, case-sensitive). The defaults match only top-level `tests/` and `test/` directories; per-library test directories need `**/tests/**`. External targets are never test targets, and a header set verification target is one exactly when the target it verifies is. Until the config file is decided, the CLI uses the defaults. CTest registration is not used because it knows only test executables.

| Rule | Applies to test targets? |
| --- | --- |
| 1 No cycles | Yes |
| 2 Definition order | Yes |
| 3 Direct layering | Yes: a test that includes T's headers must link T directly |
| 4 Entry point only | Partly: may include any target's `detail/` headers; may not include private headers |
| 5 Nesting is privacy | No: tests are left out of consumer sets |
| 6 Placement | No: tests are left out of the dominator tree |
| 7 Test isolation | Yes, from the other side: nothing outside tests may link a test target |

`detail/` is allowed because code there that grows needs its own tests, and the public API tests it only indirectly. Private headers stay forbidden because they are not on any other target's include path. A private header that needs tests should move to `detail/`.

Rule 7 closes the loophole the exemption opens: a production target placed under `tests/` escapes rules 5 and 6, but it cannot be linked from the production graph.

## Packaging and configuration

The tool is a uv-installable Python package plus a thin composite GitHub Action, living in its own repository. It uses uv, ruff for lint and format, and full type hints with a clean type checker.

| Name | Value |
| --- | --- |
| Distribution | `cpp-deps-checker` |
| Import package | `cpp_deps_checker` |
| CLI | `cpp-deps-checker check <build-dir>` |

The three names change together if the codename is replaced.

The Action (`action.yml`, composite) runs these steps, the shell parts in `action/run.sh`:

1. Install the tool with uv from the Action's own checkout.
2. Configure the consumer project with `action/configure.sh`, which the tests use too: a File API `codemodel-v2` query, the JSON trace, `CMAKE_EXPORT_COMPILE_COMMANDS=ON`, `CMAKE_OPTIMIZE_DEPENDENCIES=OFF`, and `CMAKE_VERIFY_INTERFACE_HEADER_SETS=ON`, plus the `cmake-args` input (default: the Clang compilers).
3. Write the graph with `cpp-deps-checker graph`.
4. If `base-ref` is set (default: the pull request's base branch), fetch its tip (`--depth=1` only if the clone is already shallow), check it out in a git worktree, configure the same source directory there, and write its graph as the baseline. If any of this fails, warn and leave the new edges unlisted.
5. Run `cpp-deps-checker check --annotations --summary "$GITHUB_STEP_SUMMARY"`, with `--baseline` when step 4 succeeded. It also runs the Clang `-H` pass over `compile_commands.json`.
6. Upload the graph as an artifact, then fail the job if the check failed.

The consumer repository holds one config file. Proposed name `.cpp-deps-checker.toml` (not yet decided), with these keys:

```toml
composition_roots = ["app"]

[tests]
dirs = ["tests/**", "test/**"]
names = []

[[exception]]
rule = "direct-layering"
from = "parser"
header = "lexer/detail/token.hpp"
reason = "required; why the rule is broken here"
```

## Module structure

The package is six modules in dependency order, so it reads bottom-up by the same rule it enforces. `model` and `includes` are independent leaves. `cli` is the composition root and the only place that wires concrete inputs.

```
cpp_deps_checker/
  model.py      # File API reply + trace -> Model (targets, edges, file sets, definition order, backtraces)
  includes.py   # runs clang -E -dI -H per TU -> file-level include edges with directive lines, nothing about targets
  classify.py   # Model + test patterns -> header owner and kind (public/private/detail), test-target set
  rules/        # one private module per rule (_definition_order.py, ...); __init__ exports Context, Violation, check
  report.py     # text, GitHub annotations, job summary, Mermaid graph
  cli.py        # composition root: parse args, load config, build Context, run rules, report
```

The one design decision each module hides:

- `model`: how CMake exposes targets and order (File API fields, trace format, the 4.2 field set).
- `includes`: how include edges are collected (the `-H` format today, a scanner or module maps later).
- `classify`: how files and targets are classified (which target owns a header, whether it is public, private, or `detail/`, and which targets are tests).
- `rules/*`: the policy of one rule.
- `report`: output formats and CI annotation syntax.

Rules need more than the model: rules 3 and 4 need include edges, and rules 4 to 7 need the test-target set. The `rules` package therefore owns a frozen `Context` (model, include edges, failed header set verification TUs, classification) and the `Violation` type, and every rule has the signature `check(ctx: Context) -> list[Violation]`, so `cli` runs them uniformly. `cli` builds the `Context` from the outputs of `model`, `includes`, and `classify`. Exceptions from the consumer config are applied once in `rules/__init__`, after all rules run, not inside each rule.

The package's only entry point is `rules.check(ctx) -> list[Violation]`, which runs every rule. Rule modules are private (`_cycles.py`, `_definition_order.py`), and `Context`, `Level`, and `Violation` live in a private `_core.py` that `__init__` re-exports, so rule modules do not import their own package's `__init__`. Tests go through `rules.check` too, which is how they assert that a fixture produces exactly one violation.

The declared-edges definition is computed by the model (`Target.declared_dependencies`), because which File API fields hold edges is the model's hidden decision.

Keeping `includes` free of target knowledge means replacing `-H` with another collector does not touch ownership, and changing ownership (for example for imported targets) does not touch collection.

## Testing

Tests come first. Each rule has fixture CMake projects under `tests/fixtures/<case>/` that contain exactly one violation, with a test asserting the violation and its `file:line`. The exception is a cycle: every cycle has an edge to a later-defined target, so a rule 1 fixture also violates rule 2. Rule 6 applies to every fixture, so fixtures nest each single-consumer dependency under its consumer to keep exactly one violation. Graph logic that needs no CMake (larger components, acyclic graphs, dominators) is unit-tested on a hand-built `Model`. Each fixture that configures with the default arguments also has an `expected.txt`: `# exit status: N`, then the exact standard output of `check` on it. `tests/test_expected.py` checks them through a command line, `CPP_DEPS_CHECKER` if set, so that another implementation can be held to the same behavior. The tool's own tests use pytest. CI (`.github/workflows/ci.yml`) runs them, and runs the Action itself on a clean fixture and on one with a cycle.

## C++ rewrite

The Python package above is the prototype, tagged `v0.1.0`. The goal is a C++ implementation of the same rules that also runs inside clang-tidy and clangd, so that rules 3 and 4 report in the editor as the code is typed. Until the C++ command line passes every fixture's `expected.txt`, the Python stays in the tree as the reference and gets fixes only; then it is removed.

### Why C++

- An editor integration has to run inside clang-tidy or clangd, which load C++ plugins only. Exporting a policy file from the Python tool before each editor session was rejected: it is one more step, and it goes stale when a CMakeLists.txt changes.
- Clang's preprocessor callbacks give each include directive's location and resolved file directly. That replaces pairing the `-dI` output with `-H`, and its fallbacks.

### Targets

Each feature is a CMake target, and the `target_link_libraries` graph is the dependency manifest; it must stay acyclic. The core has no LLVM dependency; only the targets under `clang/` link Clang.

```
core/
  model/      # File API reply + trace -> Model
  ownership/  # Model -> file owners and kinds, test and external targets
  rules/      # Context -> violations; also the per-include check for the plugin
cli/
  report/     # text, GitHub annotations, job summary, Mermaid graph
  main        # composition root of the command line
clang/
  includes/   # PPCallbacks -> include records, shared by the collector and the plugin
  collect/    # libTooling run over compile_commands.json (used by cli)
  tidy/       # clang-tidy module, loaded by clang-tidy --load and clangd --load
testing/      # test support: configures fixtures through action/configure.sh
```

The one design decision each target hides:

- `model`: how CMake exposes targets, edges and definition order (File API fields, trace format).
- `ownership`: which target owns a file, whether it is public, private or `detail/`, and which targets are tests or external.
- `rules`: the policy of each rule. `rules` owns the include record and the `Context` it checks, so the collectors depend on `rules`, not the other way round.
- `report`: output formats and CI annotation syntax.
- `clang/includes`: how includes are read from Clang (which callbacks, physical line from the hash location).
- `clang/collect`: how translation units are run in-process (libTooling over the compilation database). An include's line is the directive's physical line, which `#line` does not change, so unlike the Python's `-dI` pairing a file using `#line` still gets lines; a header given with `-include` counts as included by the main file, without a line, unless a directive of the main file also includes it, as in the Python.
- `clang/tidy`: how the check reaches the model from inside clang-tidy and clangd.

Allowed edges, in topological order: `model`; `ownership` -> `model`; `rules` -> `ownership`, `model`; `report` -> `rules`, `model`; `clang/includes` -> `rules`; `clang/collect` -> `clang/includes`; `clang/tidy` -> `clang/includes`, `rules`, `ownership`, `model`; the command line -> everything but `clang/tidy`. `testing` -> `model` (it builds projects by hand), and only tests link it.

### Plugin

The clang-tidy module registers checks for rules 3 and 4 only; the graph rules (1, 2, 5, 6, 7) and rule 3a need the whole project and stay in the command line. For each translation unit the check takes the compile command's directory (`ClangTidyContext::getCurrentBuildDirectory()`), walks up to the CMake build directory holding `.cmake/api/v1/reply`, and loads the codemodel there, cached per build directory and reloaded when a newer index file appears. Editors that drive CMake through the File API already leave that reply in the build directory (unverified for CMake Tools in VS Code). The trace is not needed: rules 3 and 4 use only the codemodel, and header set verification targets are recognized by their backtrace node without a line, as recorded in Inputs, instead of by a missing trace definition.

The plugin must be built with the same LLVM version as the tool that loads it, and nothing checks this at load time. clangd loads plugins from LLVM 24 ([llvm-project#226995](https://github.com/llvm/llvm-project/pull/226995)); the first target is LLVM 24 only, tested with the apt.llvm.org snapshot until 24 is released.

### Build and dependencies

- C++23, set with `target_compile_features`; CMake with FILE_SET public headers per target.
- vcpkg for third-party libraries: glaze (JSON; needs C++23, built and tested by its authors with Clang 19+ and GCC 13+), CLI11 for the command line's arguments, and Catch2 for unit tests. A usage error exits 2, as argparse does.
- LLVM 24 from the system (`find_package(LLVM 24 CONFIG)`), not vcpkg, and its `clang-cpp` library found next to it; Clang's own CMake package is not used, because on apt.llvm.org it also requires MLIR's. Only the `clang/` targets link them, for the plugin and the command line alike. libTooling finds Clang's builtin headers relative to `libclang-cpp`, so translation units get LLVM 24's whatever compiler their compile command names. Until LLVM 24 is released, CI installs apt.llvm.org's development snapshot.

### Testing

- Catch2 unit tests per target, rewritten from the Python unit tests on hand-built models.
- The fixtures and their `expected.txt` files, run against the C++ command line through `CPP_DEPS_CHECKER`; parity means all of them pass.
- The plugin: clang-tidy with `--load` on fixtures, comparing its diagnostics with the rule 3 and 4 lines of `expected.txt`.

### Open questions

- How the Action ships the C++ command line: per-platform release binaries, or a source build on the runner (which then needs LLVM 24 development packages).

## Unverified claims and open items

These claims are approximate until checked:

- `CMAKE_OPTIMIZE_DEPENDENCIES=ON` is known to strip `dependencies`; whether it also affects the four dependency fields is unknown.
- Generated headers may require a build before the `-H` pass can preprocess every TU.

Open decisions:

- [x] Graph output: a CI artifact, Mermaid in Markdown (decided 2026-11-08, over a committed docs file). The new-edge baseline therefore comes from configuring the base branch a second time on every pull request, and a base branch that does not configure leaves the new edges unlisted.
- [ ] Config file name and format (`.cpp-deps-checker.toml` proposed).
- [ ] Several targets per directory. Rule 5 is unaffected (the parent is a directory). Rule 6 accepts a target in the same directory as its dominator, so a target that should be nested in a subdirectory but shares its dominator's directory is not reported; tightening this to "strictly below" would warn on every multi-target directory.
- [ ] Multi-config generators: which configuration the check reads.
- [ ] Owner of an external target's headers (rule 3). For a fetched target the reply lists the files, but owning them would also apply rule 4: Catch2 lists its headers as sources of a static library, so they would count as private and every test including them would fail. The reply has no include directories for imported targets. Candidates: (a) inject a script through `CMAKE_PROJECT_INCLUDE` that walks the directories' `IMPORTED_TARGETS` and writes each target's `INTERFACE_INCLUDE_DIRECTORIES` with `file(GENERATE)`, a fourth input, unverified; (b) match consumer `compileGroups` include paths to `linkLibraries` entries by backtrace, ambiguous when one call links several imported targets; (c) drop imported targets from rule 3.
- [x] Type checker for the tool itself: ty.

## Sources

- [CMake 4.2 release notes](https://cmake.org/cmake/help/latest/release/4.2.html): new `linkLibraries` and related fields, interface and imported targets in replies.
- [CMake issue 21995](https://gitlab.kitware.com/cmake/cmake/-/issues/21995): `dependencies` does not separate direct from transitive.
- [CMake Discourse thread](https://discourse.cmake.org/t/how-to-traverse-target-link-libraries-dependencies-in-the-file-api/14384): `CMAKE_OPTIMIZE_DEPENDENCIES` removes dependencies from File API output.
