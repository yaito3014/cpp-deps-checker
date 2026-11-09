#include <cpp_deps_checker/report/report.hpp>
#include <cpp_deps_checker/testing/factories.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <string>
#include <vector>

namespace {

namespace fs = std::filesystem;
using namespace cpp_deps_checker;
using report::graph;
using report::kind;
using rules::level;
using testing::placed;

fs::path const source{"/repo/project"};
fs::path const repo_root{"/repo"};

rules::violation violation(std::optional<model::location> location, std::string message = "a links b", level lvl = level::error)
{
  return {"no-cycles", lvl, std::move(message), std::move(location)};
}

std::string const empty = report::mermaid(graph{});
std::string const header = empty.substr(0, empty.size() - std::string_view{"```\n"}.size());

}  // namespace

TEST_CASE("text is one compiler-style line")
{
  CHECK(report::text(violation(model::location{"a/CMakeLists.txt", 3})) == "a/CMakeLists.txt:3: error: a links b [no-cycles]");
  CHECK(report::text(violation(std::nullopt, "x", level::warning)) == "warning: x [no-cycles]");
}

TEST_CASE("an annotation's path is relative to the root")
{
  CHECK(
      report::annotation(violation(model::location{"a/CMakeLists.txt", 3}), source, repo_root)
      == "::error file=project/a/CMakeLists.txt,line=3,title=no-cycles::a links b"
  );
}

TEST_CASE("an annotation without a line or a location")
{
  CHECK(
      report::annotation(violation(model::location{"a.hpp", std::nullopt}, "a links b", level::warning), source, repo_root)
      == "::warning file=project/a.hpp,title=no-cycles::a links b"
  );
  CHECK(report::annotation(violation(std::nullopt), source, repo_root) == "::error title=no-cycles::a links b");
}

TEST_CASE("an annotation outside the root puts the location in the message")
{
  CHECK(
      report::annotation(violation(model::location{"/usr/include/x.hpp", 7}), source, repo_root) == "::error title=no-cycles::/usr/include/x.hpp:7: a links b"
  );
}

TEST_CASE("an annotation escapes data and properties")
{
  CHECK(
      report::annotation(violation(model::location{"a,b:c%.hpp", 1}, "50% done\r\nnext"), source, repo_root)
      == "::error file=project/a%2Cb%3Ac%25.hpp,line=1,title=no-cycles::50%25 done%0D%0Anext"
  );
}

TEST_CASE("the graph has declared targets and edges by name")
{
  auto const p = testing::project({
      placed("app", ".", {"lib", "ext::x"}, {.type = "EXECUTABLE"}),
      placed("lib", "lib"),
      placed("ext::x", ".", {}, {.imported = true}),
      placed("lib_test", "tests", {"lib"}, {.type = "EXECUTABLE"}),
      placed("lib_verify_interface_header_sets", "lib", {"lib"}, {.verifies = "lib"}),
      placed("docs", "docs", {}, {.type = "UTILITY"}),
      // Every edge ends at a node, even one CMake would reject.
      placed("odd", "odd", {"lib_verify_interface_header_sets", "docs"}),
  });

  CHECK(
      report::make_graph(p, {"lib_test"}, {"ext::x"})
      == graph{
          {{"app", kind::production}, {"ext::x", kind::external}, {"lib", kind::production}, {"lib_test", kind::test}, {"odd", kind::production}},
          {{"app", "lib"}, {"app", "ext::x"}, {"lib_test", "lib"}},
      }
  );
}

TEST_CASE("the graph leaves out external targets that no project target links")
{
  auto const p = testing::project({
      placed("app", ".", {"ext::x"}, {.type = "EXECUTABLE"}),
      placed("ext::x", ".", {"ext::y"}, {.imported = true}),
      placed("ext::y", ".", {}, {.imported = true}),
      placed("ext::unused", ".", {}, {.imported = true}),
  });

  CHECK(
      report::make_graph(p, {}, {"ext::x", "ext::y", "ext::unused"})
      == graph{
          {{"app", kind::production}, {"ext::x", kind::external}},
          {{"app", "ext::x"}},
      }
  );
}

TEST_CASE("mermaid lists sorted nodes, then edges")
{
  graph const g{
      {{"b", kind::production}, {"a", kind::test}, {"ext::x", kind::external}},
      {{"b", "ext::x"}, {"ext::x", "a"}, {"a", "b"}, {"b", "a"}, {"a", "ext::x"}},
  };

  CHECK(
      report::mermaid(g)
      == "```mermaid\n"
         "flowchart TD\n"
         "  classDef test stroke-dasharray: 4 4\n"
         "  classDef external font-style: italic\n"
         "  n0[\"a\"]:::test\n"
         "  n1[\"b\"]\n"
         "  n2[\"ext::x\"]:::external\n"
         "  n0 --> n1\n"
         "  n0 --> n2\n"
         "  n1 --> n0\n"
         "  n1 --> n2\n"
         "  n2 --> n0\n"
         "```\n"
  );
}

TEST_CASE("an empty graph round-trips") { CHECK(report::parse_mermaid(empty) == graph{}); }

TEST_CASE("mermaid round-trips")
{
  graph const g{
      {{"q\"uote", kind::production}, {"h#sh", kind::test}, {"#quot;", kind::external}, {"lone", kind::production}},
      {{"q\"uote", "h#sh"}, {"h#sh", "#quot;"}},
  };
  CHECK(report::parse_mermaid(report::mermaid(g)) == g);
}

TEST_CASE("parse_mermaid rejects what mermaid did not write")
{
  auto const text = GENERATE(
      std::string{}, std::string{"# Graph\n"},
      std::string{"```mermaid\nflowchart LR\n  classDef test stroke-dasharray: 4 4\n  classDef external font-style: italic\n```\n"},
      header + "  n0[\"a\"]\n  n0 --> n1\n```\n", header + "  n0[\"a\"]\n  n0 -> n0\n```\n", header + "  n0[\"a\"]:::other\n```\n", header + "  n0[\"a\"]\n"
  );
  CHECK_FALSE(report::parse_mermaid(text));
}

TEST_CASE("the summary tables violations and lists new edges")
{
  std::vector const violations{
      violation(model::location{"CMakeLists.txt", 5}, "a | b"),
      violation(std::nullopt, "a links b", level::warning),
      violation(model::location{"/usr/include/x.hpp", std::nullopt}, "line\nbreak"),
  };

  CHECK(
      report::summary(violations, source, repo_root, std::vector<report::edge>{{"a", "b"}, {"ext::x", "c"}})
      == "## cpp-deps-checker\n"
         "\n"
         "2 errors, 1 warning.\n"
         "\n"
         "| Location | Level | Rule | Message |\n"
         "| --- | --- | --- | --- |\n"
         "| `project/CMakeLists.txt:5` | error | no-cycles | a \\| b |\n"
         "|  | warning | no-cycles | a links b |\n"
         "| `/usr/include/x.hpp` | error | no-cycles | line break |\n"
         "\n"
         "### New edges\n"
         "\n"
         "- `a` -> `b`\n"
         "- `ext::x` -> `c`\n"
  );
}

TEST_CASE("the summary without violations or new edges")
{
  CHECK(report::summary({}, source, repo_root, std::vector<report::edge>{}) == "## cpp-deps-checker\n\nNo violations.\n\n### New edges\n\nNo new edges.\n");
  CHECK(
      report::summary({}, source, repo_root, std::nullopt)
      == "## cpp-deps-checker\n\nNo violations.\n\n### New edges\n\nNo baseline graph, so new edges are not listed.\n"
  );
}

TEST_CASE("the summary counts one error and several warnings")
{
  std::vector const violations{violation(std::nullopt), violation(std::nullopt, "w", level::warning), violation(std::nullopt, "w", level::warning)};
  CHECK_THAT(report::summary(violations, source, repo_root, std::nullopt), Catch::Matchers::ContainsSubstring("\n1 error, 2 warnings.\n"));
}
