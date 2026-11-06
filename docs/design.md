# cpp-deps-checker: design handoff

As of 2026-11-07.

## Purpose and scope

cpp-deps-checker (codename) is a standalone CI tool that enforces a layering policy on CMake projects. The `target_link_libraries` graph is the manifest; the tool checks that it is acyclic, defined bottom-up, and that header includes follow it. The rules are listed under Rules and severity.

In scope: multi-target CMake projects on CMake 4.2 or later, built with a Clang toolchain available in CI.

Out of scope: the order of sections within a single-header library (lowest level first), use of the `detail` namespace (needs the AST), and third-party vulnerability scanning.

## Rules and severity

Seven rules fail the check, one only warns, and two produce output. Every violation carries a `file:line` from a File API backtrace or the trace, and each rule is a pure function from the rule context (see Module structure) to a list of violations.

| # | Rule | What is checked | Level |
| --- | --- | --- | --- |
| 1 | No cycles | SCC search over direct link edges (`linkLibraries`, all visibilities) | error |
| 2 | Definition order | For every edge T1 -> T2, the `add_library`/`add_executable` of T2 runs before that of T1, by trace order. Imported targets are exempt | error |
| 3 | Direct layering | A header owned by T1 that includes a header owned by T2 != T1 requires T2 in `linkLibraries(T1)`. If the includer is a public header of T1, T2 must also be in `interfaceCompileDependencies(T1)`. Includes of imported targets' headers need a declared link too | error |
| 4 | Entry point only | T2's `detail/` headers are included only by T2's own headers and by test targets. T2's private headers are included only by T2's own files | error |
| 5 | Nesting is privacy | A target nested under parent P is consumed only from inside P's subtree. An outside consumer fails with a "hoist: private detail is becoming shared API" message | error |
| 6 | Placement | A target placed above the lowest common ancestor of its consumers (dominator tree over the production graph, rooted at the composition roots) | warning |
| 7 | Test isolation | No non-test target links a test target | error |
| 8 | Graph | DOT or Mermaid generated from the same model | output |
| 9 | New edges | Edges added relative to the base branch, listed in the job summary | output |

Definitions used by the rules:

- **Header owner.** A header in a target's `HEADERS` file set is a public header of that target. A header in a target's sources, or under its source directory and in no file set, is a private header. Imported targets have neither in the File API reply; see Verified separately under Inputs.
- **Parent.** A target's parent is the target defined in the nearest ancestor directory that defines one.
- **Exceptions.** A rule may be broken only through an entry in the consumer config, and every entry must carry a `reason`.

## Inputs

The model is built from three inputs: the CMake File API for targets and edges, a CMake JSON trace for definition order, and a Clang `-H` pass for include edges.

**CMake File API, codemodel v2.9 or later (CMake 4.2+).** Read these target fields: `linkLibraries` (direct edges), `interfaceCompileDependencies` (the PUBLIC/INTERFACE set), `fileSets`, `sources`, `paths.source`, and `backtraceGraph`. Read both `targets` and `abstractTargets`, because INTERFACE libraries are reported in the latter. Configure with `CMAKE_OPTIMIZE_DEPENDENCIES=OFF`, which otherwise strips dependency information from the reply. Do not use the `dependencies` field: it mixes direct and transitive edges and omits INTERFACE targets.

**CMake trace.** Run `cmake --trace-format=json-v1 --trace-expand --trace-redirect=<file>` and take the execution order of `add_library` and `add_executable` calls. The File API cannot supply this order: a target's backtrace starts at its own `CMakeLists.txt` with no `add_subdirectory` frame, and directory objects carry no backtrace. Drop entries from `CMakeFiles/CMakeScratch` (try_compile projects).

**Include edges.** For each TU in `compile_commands.json`, run Clang with `-H -fshow-skipped-includes`, rebuild the include tree, and take its parent -> child edges. Without `-fshow-skipped-includes`, a header blocked by its include guard on a later include is not printed and that edge is lost. Rejected alternatives: a textual `#include` scanner (sees `#if`'d-out branches, so false positives on platform code), and Clang module maps with `-fmodules-strict-decluse` (Clang-only enforcement that cannot feed the graph output).

### Verified on a fixture (CMake 4.4.4, codemodel 2.11)

Fixture: `a` links `PRIVATE b PUBLIC c PRIVATE e`; `b` is an INTERFACE library linking `d`; `e` is defined in a later `add_subdirectory` than `a`.

| Field or input | Result for `a` | Consequence |
| --- | --- | --- |
| `linkLibraries` | `[b, c, e]`, no `d` | Direct edges only: the manifest |
| `interfaceLinkLibraries` | `[b, c, e]` | Includes PRIVATE deps for a static library: unusable for visibility |
| `interfaceCompileDependencies` | `[c]` | Exactly the PUBLIC set: use for rule 3 |
| `dependencies` | `[e, c]` | Omits INTERFACE `b`: unusable |
| `abstractTargets` | holds `b`, `d` | Loader must read it |
| Trace | `add_library(a)` at `CMakeLists.txt:6`, `add_library(e)` at `late/CMakeLists.txt:1` after it | Rule 2 violation is detectable |

### Verified separately (CMake 4.4.5, codemodel 2.11; Clang 23.1.6)

**Imported targets carry no include directories in the reply.** Fixture: `ext::x` is an `INTERFACE IMPORTED` library with `INTERFACE_INCLUDE_DIRECTORIES` set, and `a` links it PRIVATE. The `ext::x` object (in `abstractTargets`) has `imported: true` and no include or file-set fields. The include directory appears only in `a`'s `compileGroups[].includes`, with the backtrace of the `target_link_libraries` call, the same backtrace as the `linkLibraries` entry. Attributing an include directory to one imported target by backtrace is therefore ambiguous when one call links several imported targets. How rule 3 finds the owner of an imported target's header is an open decision.

**`-fshow-skipped-includes` prints guarded re-includes.** Fixture: `m.cpp` includes `a.hpp` then `b.hpp`, and `b.hpp` includes the already guarded `a.hpp`. Plain `-H` prints `. ./a.hpp` and `. ./b.hpp` only; with the flag it also prints `.. ./a.hpp` under `b.hpp`, so the `b.hpp -> a.hpp` edge is kept.

## Test targets

Test targets are exempt from the privacy and placement rules (5 and 6) and may include `detail/` headers, but every other rule still applies to them. Tests usually live in a separate directory, and many build tools expect that, so treating them as ordinary consumers would make rule 5 fail on almost every project.

**Identification.** The consumer config lists directory globs and name patterns, defaulting to `tests/**` and `test/**`. Any target defined under a matching directory or with a matching name is a test target, including test helper libraries. CTest registration is not used because it knows only test executables.

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

1. Configure the consumer project with a File API `codemodel-v2` query, the JSON trace, `CMAKE_EXPORT_COMPILE_COMMANDS=ON`, and `CMAKE_OPTIMIZE_DEPENDENCIES=OFF`.
2. Run the Clang `-H -fshow-skipped-includes` pass over `compile_commands.json`, preprocessing only.
3. Run `cpp-deps-checker check`, emitting `::error` and `::warning` annotations, the new-edge list in the job summary, and the graph.

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
  includes.py   # -H output per TU -> file-level include edges (path -> path), nothing about targets
  classify.py   # Model + test patterns -> header owner and kind (public/private/detail), test-target set
  rules/        # one module per rule, each check(ctx) -> list[Violation]; __init__ owns Context, Violation, exceptions
  report.py     # text, GitHub annotations, job summary, DOT/Mermaid graph
  cli.py        # composition root: parse args, load config, build Context, run rules, report
```

The one design decision each module hides:

- `model`: how CMake exposes targets and order (File API fields, trace format, the 4.2 field set).
- `includes`: how include edges are collected (the `-H` format today, a scanner or module maps later).
- `classify`: how files and targets are classified (which target owns a header, whether it is public, private, or `detail/`, and which targets are tests).
- `rules/*`: the policy of one rule.
- `report`: output formats and CI annotation syntax.

Rules need more than the model: rules 3 and 4 need include edges, and rules 4 to 7 need the test-target set. The `rules` package therefore owns a frozen `Context` (model, include edges, classification) and the `Violation` type, and every rule has the signature `check(ctx: Context) -> list[Violation]`, so `cli` runs them uniformly. `cli` builds the `Context` from the outputs of `model`, `includes`, and `classify`. Exceptions from the consumer config are applied once in `rules/__init__`, after all rules run, not inside each rule.

Keeping `includes` free of target knowledge means replacing `-H` with another collector does not touch ownership, and changing ownership (for example for imported targets) does not touch collection.

## Implementation order

Build test-first: each step starts with a fixture CMake project under `tests/fixtures/<case>/` that contains exactly one violation, and a failing test asserting the expected violation and its `file:line`.

1. **Fixture harness.** Configure a fixture into a temporary build directory with the File API query and trace enabled, and return the reply paths.
2. **Model loader.** First test reproduces the verified fixture in Inputs: `linkLibraries`, `interfaceCompileDependencies`, and `abstractTargets` behave as recorded there.
3. **Rule 2, definition order.** Fixture: a dependency defined in a later `add_subdirectory` than its consumer. Expect one error at the consumer's link.
4. **Rule 1, cycles.** Fixture: two static libraries linking each other.
5. **Include collection, header classification, and rule 3.** Fixtures: a header including a transitive-only dependency (error); a public header including a PRIVATE-linked dependency (error); the same with PUBLIC (clean). Imported-target fixtures wait for the ownership decision below.
6. **Test classification, rule 4, and rule 7.** Fixtures: a non-test target including another target's `detail/` header (error); a test doing the same (clean); a test including a private header (error); a production target linking a test helper (error).
7. **Rules 5 and 6.** Fixtures: a nested target consumed from outside its parent (error); a target placed above its consumers' common ancestor (warning, exit code 0).
8. **Report and Action.** Annotations, job summary with new edges against the base branch, graph output.

The tool's own tests use pytest.

## Unverified claims and open items

These claims are approximate until checked, most of them in step 2 or step 5:

- `linkLibraries` holds exactly the direct `target_link_libraries` entries. Verified on one five-target fixture, not against the cmake-file-api manual.
- `CMAKE_OPTIMIZE_DEPENDENCIES=ON` is known to strip `dependencies`; whether it also affects `linkLibraries` is unknown.
- Why `interfaceLinkLibraries` lists PRIVATE deps for a static library (likely link-only propagation) is inferred, not checked.
- Generated headers may require a build before the `-H` pass can preprocess every TU.

Open decisions:

- [ ] Graph output: committed docs file diffed in CI, or a CI artifact.
- [ ] Config file name and format (`.cpp-deps-checker.toml` proposed).
- [ ] One feature target per directory is assumed by the parent definition; projects with several targets per directory need a rule.
- [ ] Multi-config generators: which configuration the check reads.
- [ ] Owner of an imported target's headers (rule 3). The reply has no include directories for imported targets. Candidates: (a) inject a script through `CMAKE_PROJECT_INCLUDE` that walks the directories' `IMPORTED_TARGETS` and writes each target's `INTERFACE_INCLUDE_DIRECTORIES` with `file(GENERATE)`, a fourth input, unverified; (b) match consumer `compileGroups` include paths to `linkLibraries` entries by backtrace, ambiguous when one call links several imported targets; (c) drop imported targets from rule 3.
- [ ] Type checker for the tool itself (mypy, pyright, or other).

## Sources

- [CMake 4.2 release notes](https://cmake.org/cmake/help/latest/release/4.2.html): new `linkLibraries` and related fields, interface and imported targets in replies.
- [CMake issue 21995](https://gitlab.kitware.com/cmake/cmake/-/issues/21995): `dependencies` does not separate direct from transitive.
- [CMake Discourse thread](https://discourse.cmake.org/t/how-to-traverse-target-link-libraries-dependencies-in-the-file-api/14384): `CMAKE_OPTIMIZE_DEPENDENCIES` removes dependencies from File API output.
