# cpp-deps-checker: design handoff

As of 2026-11-07.

## Purpose and scope

cpp-deps-checker (codename) is a standalone CI tool that enforces a layering policy on CMake projects. The `target_link_libraries` graph is the manifest; the tool checks that it is acyclic, defined bottom-up, and that header includes follow it. The rules are listed under Rules and severity.

In scope: multi-target CMake projects on CMake 4.2 or later, built with a Clang toolchain available in CI.

Out of scope: the order of sections within a single-header library (lowest level first), use of the `detail` namespace (needs the AST), and third-party vulnerability scanning.

## Rules and severity

Eight rules fail the check, one only warns, and two produce output. Every violation carries a `file:line` from a File API backtrace or the trace, and each rule is a pure function from the rule context (see Module structure) to a list of violations.

| # | Rule | What is checked | Level |
| --- | --- | --- | --- |
| 1 | No cycles | SCC search over declared edges (defined below) | error |
| 2 | Definition order | For every edge T1 -> T2, the `add_library`/`add_executable` of T2 runs before that of T1, by trace order. Imported targets are exempt | error |
| 3 | Direct layering | A file owned by T1 (source or header) that includes a header owned by T2 != T1 requires T2 in `compileDependencies(T1)` if the includer is a source or private header, and in `interfaceCompileDependencies(T1)` if it is a public header. INTERFACE libraries have only public headers. Includes of imported targets' headers need a declared link too | error |
| 3a | Self-contained headers | Every header in a target's interface header sets preprocesses on its own with that target's usage requirements: its `CMAKE_VERIFY_INTERFACE_HEADER_SETS` translation unit passes the Clang `-E` pass. Reported at the compiler's first error. Only preprocessing is checked; building the verification targets catches the rest | error |
| 4 | Entry point only | T2's `detail/` headers are included only by T2's own files and by test targets. T2's private headers are included only by T2's own files | error |
| 5 | Nesting is privacy | A target nested under a parent directory (defined below) is consumed only by targets defined in that directory or below it. An outside consumer fails at its link with a "hoist: private detail is becoming shared API" message | error |
| 6 | Placement | A target placed above the lowest common ancestor of its consumers: over the production graph, rooted at the composition roots, T's immediate dominator D is that ancestor, and T must be defined in D's directory or below it. Reported at T's definition. A target whose only dominator is the virtual root above several composition roots has no placement | warning |
| 7 | Test isolation | No non-test target links a test target | error |
| 8 | Graph | `cpp-deps-checker graph` writes the declared edges between targets, by name, as a Mermaid flowchart in a Markdown file (rendered by GitHub). Header set verification targets and UTILITY targets (which cannot be linked) are left out; test and imported targets are styled apart | output |
| 9 | New edges | Edges added relative to the base branch, listed in the job summary | output |

Definitions used by the rules:

- **Declared edges.** T1 -> T2 is a declared edge if T2's id appears in any of T1's `linkLibraries`, `compileDependencies`, `interfaceLinkLibraries`, or `interfaceCompileDependencies`, excluding entries that carry `fromDependency`. This matches what T1's `target_link_libraries` calls declare: edges out of INTERFACE libraries appear only in the two interface fields, `$<COMPILE_ONLY:...>` items only in the compile fields, and `$<LINK_ONLY:...>` items only in the link fields. Entries with `fromDependency` were injected by another target's `INTERFACE_LINK_LIBRARIES_DIRECT` and are not declared by T1. Non-target entries (`fragment`) are not edges.
- **File owner.** The owner of a file is decided in this order: (1) a target that lists the file, as a source or a file set member; (2) a target with a file set that lists no files (an abstract INTERFACE library, see Verified separately) whose base directory contains the file; (3) the target whose source directory contains the file. Within (2) and (3) the deepest directory wins. A file claimed by several targets at the same step has no owner, and so does a file no step claims. Imported targets (an open decision), UTILITY targets, and header set verification targets own nothing.
- **Public and private.** A file owned by an INTERFACE library is public. Otherwise a file is public if it is in a `PUBLIC` or `INTERFACE` file set, and private if it is in a `PRIVATE` file set, a loose source, or claimed only through the source directory.
- **Detail header.** A public file with a `detail` directory component below the directory that gave it its owner: the file set base directory that contains it (the deepest, if several do), or the owner's source directory for a loose source or a file claimed through it. A `detail/` above that directory does not count, so a target defined inside some `detail/` directory does not make all its headers detail headers. A private file is never a detail header.
- **Parent.** A target's parent directory is the nearest proper ancestor of its source directory that defines a production target other than a UTILITY target. Targets in one directory share one subtree, so the parent is a directory rather than a target, which works the same when a directory defines several targets. A parent at the top-level source directory constrains nothing.
- **Production target.** A target that is not imported, not a test target, and not a header set verification target.
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

**Include edges.** For each TU in `compile_commands.json`, the tool runs the compile command (minus `-c` and `-o`) with `-E -H -fshow-skipped-includes`, rebuild the include tree, and take its parent -> child edges. Without `-fshow-skipped-includes`, a header blocked by its include guard on a later include is not printed and that edge is lost. Only edges whose includer has an owner are kept. `-H` gives no line numbers, so the line of each kept edge is found in the includer's text: the `#include` (or `#include_next`, `#import`) whose spelled path is the longest suffix of the included path; a macro-spelled include gets no line. A header that no TU includes contributes no edges. Configuring with `CMAKE_VERIFY_INTERFACE_HEADER_SETS=ON` gives every interface header set member a TU of its own, so a header-only library is checked without a consumer. A failing header set verification TU is a rule 3a violation, and the edges it printed before failing are kept. Any other TU that fails to preprocess stops the check (exit status 2), because its include edges are incomplete. Rejected alternatives: a textual `#include` scanner (sees `#if`'d-out branches, so false positives on platform code), and Clang module maps with `-fmodules-strict-decluse` (Clang-only enforcement that cannot feed the graph output).

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

**`-fshow-skipped-includes` prints guarded re-includes.** Fixture: `m.cpp` includes `a.hpp` then `b.hpp`, and `b.hpp` includes the already guarded `a.hpp`. Plain `-H` prints `. ./a.hpp` and `. ./b.hpp` only; with the flag it also prints `.. ./a.hpp` under `b.hpp`, so the `b.hpp -> a.hpp` edge is kept.

## Test targets

Test targets are exempt from the privacy and placement rules (5 and 6) and may include `detail/` headers, but every other rule still applies to them. Tests usually live in a separate directory, and many build tools expect that, so treating them as ordinary consumers would make rule 5 fail on almost every project.

**Identification.** The consumer config lists directory globs and name patterns, defaulting to `tests/**` and `test/**` with no name patterns. A target is a test target, including a test helper library, if the path of the `CMakeLists.txt` that defines it, relative to the top-level source directory, matches a directory glob (`pathlib.PurePath.full_match`, where `**` spans any number of segments), or if its name matches a name pattern (`fnmatch`, case-sensitive). The defaults match only top-level `tests/` and `test/` directories; per-library test directories need `**/tests/**`. Imported targets are never test targets, and a header set verification target is one exactly when the target it verifies is. Until the config file is decided, the CLI uses the defaults. CTest registration is not used because it knows only test executables.

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

The Action runs these steps:

1. Configure the consumer project with a File API `codemodel-v2` query, the JSON trace, `CMAKE_EXPORT_COMPILE_COMMANDS=ON`, `CMAKE_OPTIMIZE_DEPENDENCIES=OFF`, and `CMAKE_VERIFY_INTERFACE_HEADER_SETS=ON`.
2. Run `cpp-deps-checker check`, which also runs the Clang `-H` pass over `compile_commands.json`, emitting `::error` and `::warning` annotations, the new-edge list in the job summary, and the graph.

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
  includes.py   # runs clang -H per TU -> file-level include edges with directive lines, nothing about targets
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

## Implementation order

Build test-first: each step starts with a fixture CMake project under `tests/fixtures/<case>/` that contains exactly one violation, and a failing test asserting the expected violation and its `file:line`. The exception is a cycle: every cycle has an edge to a later-defined target, so a rule 1 fixture also violates rule 2. Graph logic that needs no CMake (larger components, acyclic graphs) is unit-tested on a hand-built `Model`.

1. **Fixture harness.** Configure a fixture into a temporary build directory with the File API query and trace enabled, and return the reply paths.
2. **Model loader.** First test reproduces the verified fixture in Inputs: the four dependency fields and `abstractTargets` behave as recorded there, including INTERFACE library `b`'s edge to `d`. Then: an `error-*.json` index newer than the `index-*.json` makes the loader fail.
3. **Rule 2, definition order.** Fixture: a dependency defined in a later `add_subdirectory` than its consumer. Expect one error at the consumer's link. Also: an imported dependency defined after its consumer (clean), and model fixtures for trace matching (a try_compile project defining a same-named target, an upper-case `ADD_LIBRARY`, a wrapper function).
4. **Rule 1, cycles.** Fixture: two static libraries linking each other; expect the cycle and the rule 2 error at the same link. One violation per strongly connected component, with the shortest cycle through its first member in the message. Self-loops need no handling: CMake rejects a non-INTERFACE target linking itself and drops an INTERFACE library's self-link from the reply (verified on CMake 4.4.5).
5. **Include collection, header classification, and rule 3.** Fixtures: a header including a transitive-only dependency (error, which also needs `-fshow-skipped-includes`: the second include of the guarded header is skipped); a public header including a PRIVATE-linked dependency (error); the same with PUBLIC (clean). Classification and `-H` parsing are unit-tested on hand-built inputs. Without `compile_commands.json` at its default path, `check` warns and skips the include rules; an explicit `--compile-commands` that is missing is an error. Imported-target fixtures wait for the ownership decision below. Rule 3a: a public header that includes a header found only through a PRIVATE include directory (error); a TU outside header set verification that fails to preprocess (exit status 2).
6. **Test classification, rule 4, and rule 7.** Fixtures: a non-test target including another target's `detail/` header (error); a test doing the same (clean); a test including a private header, reached through its own include directory (error); a production target linking a test helper, next to a test linking the same helper (one error). Detail headers and test identification are unit-tested on hand-built models.
7. **Rules 5 and 6.** Fixtures: a nested target consumed from outside its parent (error); a target placed above its consumers' common ancestor (warning, exit code 0). Dominators (diamonds, cycles, several roots, same-directory placement) and the exemptions are unit-tested on hand-built models. Rule 6 applies to every fixture, so the earlier fixtures nest each single-consumer dependency under its consumer to keep exactly one violation.
8. **Report and Action.** Annotations, job summary with new edges against the base branch, graph output.

The tool's own tests use pytest.

## Unverified claims and open items

These claims are approximate until checked, most of them in step 2 or step 5:

- `CMAKE_OPTIMIZE_DEPENDENCIES=ON` is known to strip `dependencies`; whether it also affects the four dependency fields is unknown.
- Generated headers may require a build before the `-H` pass can preprocess every TU.

Open decisions:

- [ ] Graph output: committed docs file diffed in CI, or a CI artifact.
- [ ] Config file name and format (`.cpp-deps-checker.toml` proposed).
- [ ] Several targets per directory. Rule 5 is unaffected (the parent is a directory). Rule 6 accepts a target in the same directory as its dominator, so a target that should be nested in a subdirectory but shares its dominator's directory is not reported; tightening this to "strictly below" would warn on every multi-target directory.
- [ ] Multi-config generators: which configuration the check reads.
- [ ] Owner of an imported target's headers (rule 3). The reply has no include directories for imported targets. Candidates: (a) inject a script through `CMAKE_PROJECT_INCLUDE` that walks the directories' `IMPORTED_TARGETS` and writes each target's `INTERFACE_INCLUDE_DIRECTORIES` with `file(GENERATE)`, a fourth input, unverified; (b) match consumer `compileGroups` include paths to `linkLibraries` entries by backtrace, ambiguous when one call links several imported targets; (c) drop imported targets from rule 3.
- [x] Type checker for the tool itself: ty.

## Sources

- [CMake 4.2 release notes](https://cmake.org/cmake/help/latest/release/4.2.html): new `linkLibraries` and related fields, interface and imported targets in replies.
- [CMake issue 21995](https://gitlab.kitware.com/cmake/cmake/-/issues/21995): `dependencies` does not separate direct from transitive.
- [CMake Discourse thread](https://discourse.cmake.org/t/how-to-traverse-target-link-libraries-dependencies-in-the-file-api/14384): `CMAKE_OPTIMIZE_DEPENDENCIES` removes dependencies from File API output.
