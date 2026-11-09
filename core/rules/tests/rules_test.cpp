#include <cpp_deps_checker/config/config.hpp>
#include <cpp_deps_checker/rules/rules.hpp>
#include <cpp_deps_checker/testing/factories.hpp>
#include <cpp_deps_checker/testing/fixture.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <cstddef>
#include <format>
#include <set>
#include <stdexcept>
#include <string>
#include <tuple>
#include <vector>

namespace {

namespace fs = std::filesystem;
using namespace cpp_deps_checker;
using rules::level;
using testing::headers;
using testing::placed;
using testing::root;
using testing::target;

// Messages of one rule's violations in a project of the given targets,
// classified with the default test patterns and no include edges.
std::vector<std::string> messages(std::string_view rule, std::vector<model::target> targets)
{
  std::vector<std::string> result;
  for (auto const& v : rules::check(rules::make_context(testing::project(std::move(targets))))) {
    if (v.rule == rule) {
      result.push_back(v.message);
    }
  }
  return result;
}

using summary_entry = std::tuple<std::string, level, std::optional<model::location>>;

std::vector<summary_entry> summary(std::vector<rules::violation> const& violations)
{
  std::vector<summary_entry> result;
  for (auto const& v : violations) {
    result.emplace_back(v.rule, v.level, v.location);
  }
  return result;
}

// The graph rules on a configured fixture, without include edges.
std::vector<rules::violation> check_fixture(std::string_view fixture)
{
  testing::temporary_directory const dir;
  auto const build = testing::configure(testing::fixture(fixture), dir.path() / "build");
  auto project = model::load(build.dir, build.trace);
  if (!project) {
    throw std::runtime_error{project.error().message};
  }
  return rules::check(rules::make_context(*std::move(project)));
}

model::location at(fs::path file, int line) { return {std::move(file), line}; }

model::target defined(model::target t, std::size_t order)
{
  t.definition = model::definition{order, at("CMakeLists.txt", static_cast<int>(order))};
  return t;
}

}  // namespace

TEST_CASE("levels print as GitHub's annotation levels")
{
  CHECK(rules::to_string(level::error) == "error");
  CHECK(rules::to_string(level::warning) == "warning");
}

// Rule 1

TEST_CASE("static libraries linking each other")
{
  auto const violations = check_fixture("cycles_static_pair");

  // Every cycle has an edge to a later-defined target, so rule 2 fires too.
  CHECK(
      summary(violations)
      == std::vector<summary_entry>{
          {"no-cycles", level::error, at("CMakeLists.txt", 5)},
          {"definition-order", level::error, at("CMakeLists.txt", 5)},
      }
  );
  CHECK_THAT(violations.front().message, Catch::Matchers::ContainsSubstring("a -> b -> a"));
}

TEST_CASE("an acyclic diamond is clean")
{
  CHECK(messages("no-cycles", {placed("a", "a", {"b", "c"}), placed("b", "b", {"d"}), placed("c", "c", {"d"}), placed("d", "d")}).empty());
}

TEST_CASE("one violation per component, with the shortest cycle")
{
  CHECK(
      messages("no-cycles", {placed("a", "a", {"b"}), placed("b", "b", {"c", "a"}), placed("c", "c", {"a"}), placed("d", "d", {"e"}), placed("e", "e", {"d"})})
      == std::vector<std::string>{
          "dependency cycle: a -> b -> a (also strongly connected: c)",
          "dependency cycle: d -> e -> d",
      }
  );
}

TEST_CASE("a cycle closed only through a dependency of a dependency")
{
  CHECK(
      messages("no-cycles", {placed("a", "a", {"b"}), placed("b", "b", {"c"}), placed("c", "c", {"a"})})
      == std::vector<std::string>{"dependency cycle: a -> b -> c -> a"}
  );
}

TEST_CASE("a chain deeper than a recursive search could go")
{
  constexpr int n = 100'000;
  std::vector<model::target> targets;
  for (int i = 0; i < n; ++i) {
    targets.push_back(placed(std::format("t{}", i), std::format("t{}", i), {std::format("t{}", i + 1)}));
  }
  targets.push_back(placed(std::format("t{}", n), std::format("t{}", n), {std::format("t{}", n - 1)}));

  CHECK(messages("no-cycles", std::move(targets)) == std::vector<std::string>{std::format("dependency cycle: t{} -> t{} -> t{}", n - 1, n, n - 1)});
}

// Rule 2

TEST_CASE("a dependency defined after its consumer")
{
  CHECK(summary(check_fixture("definition_order_late")) == std::vector<summary_entry>{{"definition-order", level::error, at("CMakeLists.txt", 5)}});
}

TEST_CASE("an imported dependency is exempt") { CHECK(check_fixture("definition_order_imported").empty()); }

TEST_CASE("the definition order message")
{
  CHECK(
      messages("definition-order", {defined(placed("a", "a", {"late"}), 1), defined(placed("late", "late"), 2)})
      == std::vector<std::string>{"a links late, which is defined later at CMakeLists.txt:2; define late before a"}
  );
}

TEST_CASE("external targets are exempt from definition order")
{
  auto const fetched = fs::path{"/elsewhere"};
  CHECK(messages(
            "definition-order",
            {
                defined(placed("a", "a", {"late_dep"}), 1),
                defined(target("late_dep", {.source_dir = fetched / "dep-src"}), 2),
                defined(target("dep_user", {.source_dir = fetched / "user-src", .links = {"late"}}), 3),
                defined(placed("late", "late"), 4),
            }
  )
            .empty());
}

// Rules 3 and 4, on hand-built includes

namespace {

// a: a.cpp, private header a/src/impl.hpp, public header a/include/a/a.hpp,
// detail header a/include/a/detail/d.hpp; it links b PRIVATE and c PUBLIC.
// b, c, d: header-only libraries in b/, c/, d/; a_test in tests/.
rules::context includes_context(std::vector<rules::include> includes)
{
  auto a = target(
      "a", {.type = "STATIC_LIBRARY",
            .sources = {root / "a/a.cpp"},
            .file_sets = {
                headers("PUBLIC", {root / "a/include/a/a.hpp", root / "a/include/a/detail/d.hpp"}, {root / "a/include"}),
                headers("PRIVATE", {root / "a/src/impl.hpp"}, {root / "a/src"}),
            }}
  );
  a.link_libraries = {{"b", std::nullopt}, {"c", std::nullopt}};
  a.compile_dependencies = {{"b", std::nullopt}, {"c", std::nullopt}};
  a.interface_link_libraries = {{"c", std::nullopt}};
  a.interface_compile_dependencies = {{"c", std::nullopt}};
  auto a_test = target("a_test", {.type = "EXECUTABLE", .source_dir = root / "tests", .sources = {root / "tests/a_test.cpp"}});
  a_test.link_libraries = {{"a", std::nullopt}};
  a_test.compile_dependencies = {{"a", std::nullopt}};
  auto ctx = rules::make_context(testing::project({a, target("b"), target("c"), target("d"), a_test}));
  ctx.includes = std::move(includes);
  return ctx;
}

std::vector<rules::violation> of_rule(std::string_view rule, std::vector<rules::violation> const& violations)
{
  std::vector<rules::violation> result;
  for (auto const& v : violations) {
    if (v.rule == rule) {
      result.push_back(v);
    }
  }
  return result;
}

}  // namespace

TEST_CASE("a source including a target it does not link")
{
  auto const ctx = includes_context({{root / "a/a.cpp", root / "d/d.hpp", 2}});
  CHECK(
      of_rule("direct-layering", rules::check(ctx))
      == std::vector<rules::violation>{
          {"direct-layering", level::error, "a includes d/d.hpp of d without linking d directly", at("a/a.cpp", 2)},
      }
  );
}

TEST_CASE("a public header including a PRIVATE-linked target")
{
  auto const ctx = includes_context({{root / "a/include/a/a.hpp", root / "b/b.hpp", 3}});
  CHECK(
      of_rule("direct-layering", rules::check(ctx))
      == std::vector<rules::violation>{
          {"direct-layering", level::error, "public header of a includes b/b.hpp of b, which a links PRIVATE; link it PUBLIC", at("a/include/a/a.hpp", 3)},
      }
  );
}

TEST_CASE("a public header including a target it does not link")
{
  auto const ctx = includes_context({{root / "a/include/a/a.hpp", root / "d/d.hpp", std::nullopt}});
  CHECK(
      of_rule("direct-layering", rules::check(ctx))
      == std::vector<rules::violation>{
          {"direct-layering", level::error, "public header of a includes d/d.hpp of d, which a does not link; link it PUBLIC",
           model::location{"a/include/a/a.hpp", std::nullopt}},
      }
  );
}

TEST_CASE("includes that follow the links are clean")
{
  auto const ctx = includes_context({
      {root / "a/a.cpp", root / "b/b.hpp", 1},                     // linked PRIVATE
      {root / "a/a.cpp", root / "a/src/impl.hpp", 2},              // own private header
      {root / "a/include/a/a.hpp", root / "c/c.hpp", 1},           // linked PUBLIC
      {root / "a/a.cpp", "/usr/include/vector", 3},                // unowned
      {root / "tests/a_test.cpp", root / "a/include/a/a.hpp", 1},  // a_test links a
  });
  // Only the include rules: rule 6 does warn that b and c belong under a/.
  auto const violations = rules::check(ctx);
  CHECK(of_rule("direct-layering", violations).empty());
  CHECK(of_rule("entry-point", violations).empty());
}

TEST_CASE("including another target's private header")
{
  auto const ctx = includes_context({{root / "tests/a_test.cpp", root / "a/src/impl.hpp", 4}});
  CHECK(
      of_rule("entry-point", rules::check(ctx))
      == std::vector<rules::violation>{
          {"entry-point", level::error, "a_test includes a/src/impl.hpp, a private header of a; only a may include it", at("tests/a_test.cpp", 4)},
      }
  );
}

TEST_CASE("a production target including another target's detail header")
{
  auto b = target("b", {.type = "STATIC_LIBRARY", .sources = {root / "b/b.cpp"}});
  b.compile_dependencies = {{"a", std::nullopt}};
  auto ctx = includes_context({});
  ctx = [&] {
    auto targets = ctx.project.targets();
    std::erase_if(targets, [](model::target const& t) { return t.id == "b"; });
    targets.push_back(b);
    auto fresh = rules::make_context(testing::project(std::move(targets)));
    fresh.includes = {{root / "b/b.cpp", root / "a/include/a/detail/d.hpp", 2}};
    return fresh;
  }();

  CHECK(
      of_rule("entry-point", rules::check(ctx))
      == std::vector<rules::violation>{
          {"entry-point", level::error, "b includes a/include/a/detail/d.hpp, a detail header of a; only a and test targets may include it", at("b/b.cpp", 2)},
      }
  );
}

TEST_CASE("a test target including a detail header")
{
  auto const ctx = includes_context({{root / "tests/a_test.cpp", root / "a/include/a/detail/d.hpp", 2}});
  CHECK(of_rule("entry-point", rules::check(ctx)).empty());
}

TEST_CASE("check_include runs rules 3 and 4 on one include")
{
  auto const ctx = includes_context({});
  CHECK(
      rules::check_include(ctx, {root / "tests/a_test.cpp", root / "d/d.hpp", 1})
      == std::vector<rules::violation>{
          {"direct-layering", level::error, "a_test includes d/d.hpp of d without linking d directly", at("tests/a_test.cpp", 1)},
      }
  );
  CHECK(
      rules::check_include(ctx, {root / "tests/a_test.cpp", root / "a/src/impl.hpp", 2})
      == std::vector<rules::violation>{
          {"entry-point", level::error, "a_test includes a/src/impl.hpp, a private header of a; only a may include it", at("tests/a_test.cpp", 2)},
      }
  );
}

// Rule 3a

namespace {

rules::context verifying_context(std::vector<rules::failure> failures)
{
  auto const verifying = [](std::string const& name, fs::path const& source_dir) {
    return std::vector{
        target(name, {.source_dir = source_dir}),
        target(
            name + "_verify_interface_header_sets",
            {.type = "OBJECT_LIBRARY", .source_dir = source_dir, .sources = {testing::default_build / (name + ".cxx")}, .verifies = name}
        ),
    };
  };
  auto targets = verifying("vendored", "/elsewhere/vendored");
  targets.append_range(verifying("a", root / "a"));
  auto ctx = rules::make_context(testing::project(std::move(targets)));
  ctx.failures = std::move(failures);
  return ctx;
}

}  // namespace

TEST_CASE("a header that does not preprocess on its own")
{
  auto const ctx = verifying_context({{testing::default_build / "a.cxx", rules::diagnostic{root / "a/a.hpp", 1, "'config.hpp' file not found"}, ""}});
  CHECK(
      rules::check(ctx)
      == std::vector<rules::violation>{
          {"self-contained", level::error, "header of a does not preprocess on its own: 'config.hpp' file not found", at("a/a.hpp", 1)},
      }
  );
}

TEST_CASE("header sets of external targets need not be self-contained")
{
  auto const ctx = verifying_context({{testing::default_build / "vendored.cxx", std::nullopt, ""}, {testing::default_build / "a.cxx", std::nullopt, ""}});
  CHECK(
      of_rule("self-contained", rules::check(ctx))
      == std::vector<rules::violation>{
          {"self-contained", level::error, "header set verification of a failed to preprocess /build/a.cxx", std::nullopt},
      }
  );
}

// Rule 5

TEST_CASE("a consumer outside the parent directory")
{
  auto const violations = check_fixture("nesting_outside_parent");
  CHECK(summary(violations) == std::vector<summary_entry>{{"nesting", level::error, at("CMakeLists.txt", 5)}});
  CHECK(violations.front().message == "app links lexer, which is private to parser/; hoist lexer: a private detail is becoming shared API");
}

TEST_CASE("consumers inside the parent directory are fine")
{
  CHECK(
      messages("nesting", {placed("parser", "parser", {"lexer"}), placed("lexer", "parser/lexer"), placed("parser_extra", "parser/extra", {"lexer"})}).empty()
  );
}

TEST_CASE("the parent is the nearest directory defining a target")
{
  // parser/impl defines nothing, so lexer is private to parser/.
  CHECK(
      messages("nesting", {placed("parser", "parser"), placed("lexer", "parser/impl/lexer"), placed("app", "app", {"lexer"})})
      == std::vector<std::string>{"app links lexer, which is private to parser/; hoist lexer: a private detail is becoming shared API"}
  );
}

TEST_CASE("targets without a defining ancestor are shared")
{
  CHECK(messages("nesting", {placed("docs", "libs", {}, {.type = "UTILITY"}), placed("lexer", "libs/lexer"), placed("app", "app", {"lexer"})}).empty());
}

TEST_CASE("the top-level directory constrains nothing")
{
  CHECK(messages("nesting", {placed("top", "."), placed("lexer", "libs/lexer"), placed("app", "app", {"lexer"})}).empty());
}

TEST_CASE("test targets are exempt both ways")
{
  CHECK(messages(
            "nesting",
            {
                placed("parser", "parser"),
                placed("lexer", "parser/lexer"),
                placed("lexer_test", "tests", {"lexer"}),
                placed("tests_base", "tests"),
                placed("helper", "tests/unit/helper"),
                placed("other_test", "tests/other", {"helper"}),
            }
  )
            .empty());
}

TEST_CASE("verification and imported targets are exempt")
{
  CHECK(messages(
            "nesting",
            {
                placed("parser", "parser"),
                placed("lexer", "parser/lexer"),
                placed("v", "elsewhere", {"lexer"}, {.verifies = "lexer"}),
                placed("ext", "elsewhere", {"lexer"}, {.imported = true}),
            }
  )
            .empty());
}

// Rule 6

TEST_CASE("a target placed above its only consumer")
{
  auto const violations = check_fixture("placement_above_consumers");
  CHECK(summary(violations) == std::vector<summary_entry>{{"placement", level::warning, at("lexer/CMakeLists.txt", 1)}});
  CHECK(violations.front().message == "every path to lexer goes through parser; move lexer under parser/");
}

TEST_CASE("the dominator is the lowest common ancestor of the consumers")
{
  // ast is reached through both parser and printer, so its dominator is app;
  // token is also reached from parser directly, so app as well.
  CHECK(messages(
            "placement",
            {
                placed("app", ".", {"parser", "printer"}),
                placed("parser", "parser", {"ast", "token"}),
                placed("printer", "printer", {"ast"}),
                placed("ast", "ast", {"token"}),
                placed("token", "token"),
            }
  )
            .empty());
}

TEST_CASE("nested under the dominator is fine")
{
  CHECK(messages("placement", {placed("app", ".", {"parser"}), placed("parser", "parser", {"lexer"}), placed("lexer", "parser/lexer")}).empty());
}

TEST_CASE("the same directory as the dominator is fine")
{
  CHECK(messages("placement", {placed("app", ".", {"parser"}), placed("parser", "parser", {"parser_obj"}), placed("parser_obj", "parser")}).empty());
}

TEST_CASE("a target shared by two roots has no placement")
{
  CHECK(messages("placement", {placed("cli", "cli", {"core"}), placed("gui", "gui", {"core"}), placed("core", "core")}).empty());
}

TEST_CASE("each unconsumed target is a root")
{
  // lib is a root of its own; util is reached through lib only.
  CHECK(
      messages("placement", {placed("lib", "lib", {"util"}), placed("util", "util")})
      == std::vector<std::string>{"every path to util goes through lib; move util under lib/"}
  );
}

TEST_CASE("the dominator through a diamond")
{
  // app -> {a, b} -> c -> d: c and d are dominated by app, d by c.
  CHECK(
      messages(
          "placement",
          {
              placed("app", "app", {"a", "b"}),
              placed("a", "app/a", {"c"}),
              placed("b", "app/b", {"c"}),
              placed("c", "app/c", {"d"}),
              placed("d", "d"),
          }
      )
      == std::vector<std::string>{"every path to d goes through c; move d under app/c/"}
  );
}

TEST_CASE("a cycle reached from a root")
{
  CHECK(
      messages("placement", {placed("app", "app", {"a"}), placed("a", "app/a", {"b"}), placed("b", "b", {"a"})})
      == std::vector<std::string>{"every path to b goes through a; move b under app/a/"}
  );
}

TEST_CASE("tests and verification targets are not consumers")
{
  CHECK(
      messages(
          "placement",
          {
              placed("lib", "lib", {"util"}),
              placed("util", "util"),
              placed("util_test", "tests", {"util"}),
              placed("util_verify", "util", {"util"}, {.verifies = "util"}),
          }
      )
      == std::vector<std::string>{"every path to util goes through lib; move util under lib/"}
  );
}

// Rule 7

TEST_CASE("a production target linking a test helper")
{
  auto const violations = check_fixture("test_isolation_helper");
  CHECK(summary(violations) == std::vector<summary_entry>{{"test-isolation", level::error, at("app/CMakeLists.txt", 2)}});
  CHECK(violations.front().message == "app links test target test_helper; only test targets may link it");
}

// Classification feeding the rules

TEST_CASE("external targets are neither tests nor production")
{
  auto const ctx = rules::make_context(
      testing::project(
          {
              target("fetched", {.source_dir = root / "build/_deps/dep-src"}),
              target("outside", {.source_dir = "/elsewhere/tests"}),
              target("a_test", {.source_dir = root / "tests"}),
              target("a", {.source_dir = root / "a"}),
          },
          root / "build"
      )
  );

  CHECK(ctx.tests == std::set<std::string>{"a_test"});
  REQUIRE(ctx.production().size() == 1);
  CHECK(ctx.production().front()->id == "a");
}

namespace {

std::vector<std::string> configured_messages(std::string_view rule, std::vector<model::target> targets, config::config const& settings)
{
  auto const ctx = rules::make_context(testing::project(std::move(targets)), settings);
  if (!ctx) {
    throw std::runtime_error{ctx.error()};
  }
  std::vector<std::string> result;
  for (auto const& v : rules::check(*ctx)) {
    if (v.rule == rule) {
      result.push_back(v.message);
    }
  }
  return result;
}

}  // namespace

TEST_CASE("the configured composition roots replace the unconsumed targets")
{
  // gui links core too, but is not a composition root.
  auto const targets = std::vector{placed("cli", "cli", {"core"}), placed("gui", "gui", {"core"}), placed("core", "core")};

  CHECK(
      configured_messages("placement", targets, {.composition_roots = std::vector<std::string>{"cli"}})
      == std::vector<std::string>{"every path to core goes through cli; move core under cli/"}
  );
}

TEST_CASE("a composition root must name a production target")
{
  auto const project = testing::project({placed("app", "app"), placed("app_test", "tests", {"app"})});
  auto const name = GENERATE(std::string{"missing"}, std::string{"app_test"});
  CAPTURE(name);

  auto const ctx = rules::make_context(project, {.composition_roots = std::vector<std::string>{name}});

  REQUIRE_FALSE(ctx);
  CHECK_THAT(ctx.error(), Catch::Matchers::ContainsSubstring(std::format("composition_roots: no production target named {}", name)));
}

TEST_CASE("the configured test patterns replace the defaults")
{
  auto const project = testing::project({placed("a", "a"), placed("a_check", "a", {"a"}), placed("a_test", "tests", {"a"})});
  config::config const settings{.tests = config::tests_config{.directories = std::vector<std::string>{}, .names = std::vector<std::string>{"*_check"}}};

  auto const ctx = rules::make_context(project, settings);

  REQUIRE(ctx);
  CHECK(ctx->tests == std::set<std::string>{"a_check"});
}

TEST_CASE("an empty config keeps the defaults")
{
  auto const project = testing::project({placed("a", "a"), placed("a_test", "tests", {"a"})});

  auto const ctx = rules::make_context(project, {});

  REQUIRE(ctx);
  CHECK(ctx->tests == std::set<std::string>{"a_test"});
}
