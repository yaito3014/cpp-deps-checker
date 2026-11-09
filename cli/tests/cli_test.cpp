#include <cpp_deps_checker/cli/cli.hpp>
#include <cpp_deps_checker/model/model.hpp>
#include <cpp_deps_checker/ownership/ownership.hpp>
#include <cpp_deps_checker/report/report.hpp>
#include <cpp_deps_checker/testing/fixture.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <fstream>
#include <initializer_list>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace {

namespace fs = std::filesystem;
using namespace cpp_deps_checker;
using Catch::Matchers::ContainsSubstring;
using Catch::Matchers::EndsWith;
using Catch::Matchers::StartsWith;
using testing::temporary_directory;

struct result {
  int status;
  std::string out;
  std::string err;
};

result run(std::initializer_list<std::string> args)
{
  std::vector<std::string> argv{"cpp-deps-checker"};
  argv.append_range(args);
  std::ostringstream out;
  std::ostringstream err;
  int const status = cli::run(argv, out, err);
  return {status, out.str(), err.str()};
}

std::vector<std::string> lines(std::string const& text)
{
  std::vector<std::string> result;
  std::istringstream stream{text};
  for (std::string line; std::getline(stream, line);) {
    result.push_back(line);
  }
  return result;
}

std::string read(fs::path const& file)
{
  std::ifstream in{file};
  return {std::istreambuf_iterator<char>{in}, {}};
}

testing::build configured(std::string_view fixture, temporary_directory const& dir)
{
  return testing::configure(testing::fixture(fixture), dir.path() / "build");
}

// Changes the working directory for one test.
class working_directory {
public:
  explicit working_directory(fs::path const& path) : previous_{fs::current_path()} { fs::current_path(path); }
  ~working_directory() { fs::current_path(previous_); }
  working_directory(working_directory const&) = delete;
  working_directory& operator=(working_directory const&) = delete;

private:
  fs::path previous_;
};

}  // namespace

TEST_CASE("violations are printed and fail")
{
  temporary_directory const dir;
  auto const build = configured("definition_order_late", dir);

  auto const r = run({"check", build.dir.string()});

  CHECK(r.status == 1);
  auto const printed = lines(r.out);
  REQUIRE(printed.size() == 1);
  CHECK_THAT(printed.front(), StartsWith("CMakeLists.txt:5: error: a links late") && EndsWith(" [definition-order]"));
}

TEST_CASE("a clean project passes silently")
{
  temporary_directory const dir;
  auto const r = run({"check", configured("definition_order_imported", dir).dir.string()});
  CHECK(r.status == 0);
  CHECK(r.out.empty());
}

TEST_CASE("the trace option overrides the default")
{
  temporary_directory const dir;
  auto const build = configured("definition_order_late", dir);
  auto const moved = dir.path() / "elsewhere.json";
  fs::rename(build.trace, moved);

  CHECK(run({"check", build.dir.string(), "--trace", moved.string()}).status == 1);
}

TEST_CASE("unloadable input is reported")
{
  temporary_directory const dir;
  auto const r = run({"check", dir.path().string()});
  CHECK(r.status == 2);
  CHECK_THAT(r.err, ContainsSubstring("no File API reply"));
}

TEST_CASE("the executable runs the command line")
{
  temporary_directory const dir;
  auto const build = configured("definition_order_imported", dir);
  std::vector<std::string> const command{CPP_DEPS_CHECKER_EXECUTABLE, "check", build.dir.string()};
  CHECK_NOTHROW(testing::run(command));
}

TEST_CASE("a missing default compilation database warns and skips the include rules")
{
  temporary_directory const dir;
  auto const r = run({"check", configured("definition_order_imported", dir).dir.string()});
  CHECK(r.status == 0);
  CHECK_THAT(r.err, ContainsSubstring("include rules skipped"));
}

TEST_CASE("a missing explicit compilation database fails")
{
  temporary_directory const dir;
  auto const build = configured("definition_order_imported", dir);
  auto const r = run({"check", build.dir.string(), "--compile-commands", (dir.path() / "nowhere.json").string()});
  CHECK(r.status == 2);
  CHECK_THAT(r.err, ContainsSubstring("nowhere.json"));
}

TEST_CASE("an include violation through the command line")
{
  temporary_directory const dir;
  auto const r = run({"check", configured("direct_layering_transitive", dir).dir.string()});
  CHECK(r.status == 1);
  auto const printed = lines(r.out);
  REQUIRE(printed.size() == 1);
  CHECK_THAT(printed.front(), StartsWith("a/a.cpp:2: error: "));
}

TEST_CASE("a failure outside header set verification is unusable input")
{
  temporary_directory const dir;
  auto const r = run({"check", configured("includes_broken_source", dir).dir.string()});
  CHECK(r.status == 2);
  CHECK_THAT(r.err, ContainsSubstring("missing.hpp"));
}

TEST_CASE("warnings only pass")
{
  temporary_directory const dir;
  auto const r = run({"check", configured("placement_above_consumers", dir).dir.string()});
  CHECK(r.status == 0);
  auto const printed = lines(r.out);
  REQUIRE(printed.size() == 1);
  CHECK_THAT(printed.front(), StartsWith("lexer/CMakeLists.txt:1: warning: "));
}

TEST_CASE("annotations are relative to the working directory")
{
  temporary_directory const dir;
  auto const build = configured("definition_order_late", dir);
  working_directory const in{testing::fixture("")};

  auto const r = run({"check", build.dir.string(), "--annotations"});

  CHECK(r.status == 1);
  auto const printed = lines(r.out);
  REQUIRE(printed.size() == 1);
  CHECK_THAT(printed.front(), StartsWith("::error file=definition_order_late/CMakeLists.txt,line=5,title=definition-order::a links late"));
}

TEST_CASE("the graph is written without checking")
{
  temporary_directory const dir;
  auto const build = configured("definition_order_late", dir);
  auto const output = dir.path() / "graph.md";

  CHECK(run({"graph", build.dir.string(), "--output", output.string()}).status == 0);

  using report::kind;
  CHECK(
      report::parse_mermaid(read(output))
      == report::graph{{{"a", kind::production}, {"base", kind::production}, {"late", kind::production}}, {{"a", "base"}, {"a", "late"}}}
  );
  auto const r = run({"graph", build.dir.string()});
  CHECK(r.status == 0);
  CHECK(r.out == read(output));
}

TEST_CASE("the graph of unloadable input fails")
{
  temporary_directory const dir;
  auto const r = run({"graph", dir.path().string()});
  CHECK(r.status == 2);
  CHECK_THAT(r.err, ContainsSubstring("no File API reply"));
}

TEST_CASE("the summary is appended, with new edges against the baseline")
{
  temporary_directory const dir;
  auto const build = configured("definition_order_late", dir);
  auto const baseline = dir.path() / "base.md";
  using report::kind;
  std::ofstream{baseline} << report::mermaid({{{"a", kind::production}, {"base", kind::production}}, {{"a", "base"}}});
  auto const summary = dir.path() / "summary.md";
  std::ofstream{summary} << "earlier step\n";
  working_directory const in{testing::fixture("")};

  CHECK(run({"check", build.dir.string(), "--summary", summary.string(), "--baseline", baseline.string()}).status == 1);

  auto const text = read(summary);
  CHECK_THAT(text, StartsWith("earlier step\n## cpp-deps-checker\n"));
  CHECK_THAT(text, ContainsSubstring("| `definition_order_late/CMakeLists.txt:5` | error |"));
  CHECK_THAT(text, EndsWith("### New edges\n\n- `a` -> `late`\n"));
}

TEST_CASE("the summary without a baseline")
{
  temporary_directory const dir;
  auto const summary = dir.path() / "summary.md";
  CHECK(run({"check", configured("definition_order_imported", dir).dir.string(), "--summary", summary.string()}).status == 0);
  CHECK_THAT(read(summary), EndsWith("new edges are not listed.\n"));
}

TEST_CASE("the baseline needs a summary")
{
  temporary_directory const dir;
  auto const r = run({"check", dir.path().string(), "--baseline", (dir.path() / "base.md").string()});
  CHECK(r.status == 2);
  CHECK_THAT(r.err, ContainsSubstring("--baseline requires --summary"));
}

TEST_CASE("an unreadable baseline is unusable input")
{
  temporary_directory const dir;
  auto const build = configured("definition_order_imported", dir);
  auto const baseline = dir.path() / "base.md";
  std::ofstream{baseline} << "# not a graph\n";
  auto const summary = dir.path() / "summary.md";

  auto const r = run({"check", build.dir.string(), "--summary", summary.string(), "--baseline", baseline.string()});

  CHECK(r.status == 2);
  CHECK_THAT(r.err, ContainsSubstring("not a Mermaid graph"));
  CHECK_FALSE(fs::exists(summary));
}

TEST_CASE("a missing baseline is unusable input")
{
  temporary_directory const dir;
  auto const build = configured("definition_order_imported", dir);
  auto const r = run({"check", build.dir.string(), "--summary", (dir.path() / "s.md").string(), "--baseline", (dir.path() / "none.md").string()});
  CHECK(r.status == 2);
  CHECK_THAT(r.err, ContainsSubstring("none.md"));
}

TEST_CASE("a usage error exits 2, as argparse does")
{
  CHECK(run({}).status == 2);
  CHECK(run({"check"}).status == 2);
  CHECK(run({"check", "build", "--unknown"}).status == 2);
}

TEST_CASE("help exits 0")
{
  auto const r = run({"--help"});
  CHECK(r.status == 0);
  CHECK_THAT(r.out, ContainsSubstring("check"));
}

TEST_CASE("a graph that cannot be written fails")
{
  temporary_directory const dir;
  auto const build = configured("definition_order_late", dir);
  auto const output = dir.path() / "missing" / "graph.md";

  auto const r = run({"graph", build.dir.string(), "--output", output.string()});

  CHECK(r.status == 2);
  CHECK_THAT(r.err, ContainsSubstring(output.string()));
}

TEST_CASE("a summary that cannot be written fails, even when the check is clean")
{
  temporary_directory const dir;
  auto const build = configured("definition_order_imported", dir);

  auto const r = run({"check", build.dir.string(), "--summary", dir.path().string()});

  CHECK(r.status == 2);
  CHECK_THAT(r.err, ContainsSubstring(dir.path().string()));
}

TEST_CASE("a dependency fetched into a build directory inside the source tree is external")
{
  temporary_directory const dir;
  auto const source = dir.path() / "source";
  fs::copy(testing::fixture("external_fetched"), source, fs::copy_options::recursive);
  auto const build = testing::configure(source, source / "build");

  auto const project = model::load(build.dir, build.trace);
  REQUIRE(project);
  std::set<std::string> external;
  for (auto const& id : ownership::find_external(*project)) {
    external.insert(project->find(id)->name);
  }
  CHECK(external == std::set<std::string>{"dep", "dep_main"});
  auto const r = run({"check", build.dir.string()});
  CHECK(r.status == 0);
  CHECK(r.out.empty());
}

// With Ninja and C++20, CMake adds a module map that only a build writes to
// each compile command.
TEST_CASE("a C++20 project is checked before its first build")
{
  temporary_directory const dir;
  std::vector<std::string> const cxx20{"-DCMAKE_CXX_STANDARD=20"};
  auto const build = testing::configure(testing::fixture("direct_layering_transitive"), dir.path() / "build", cxx20);
  REQUIRE_THAT(read(build.compile_commands), ContainsSubstring(".modmap"));

  auto const r = run({"check", build.dir.string()});

  CAPTURE(r.err);
  CHECK(r.status == 1);
  CHECK(lines(r.out) == std::vector<std::string>{"a/a.cpp:2: error: a includes a/b/c/include/c/c.hpp of c without linking c directly [direct-layering]"});
}

TEST_CASE("a baseline that cannot be read is unusable input")
{
  temporary_directory const dir;
  auto const build = configured("definition_order_imported", dir);
  auto const baseline = dir.path() / "base.md";
  fs::create_directory(baseline);

  auto const r = run({"check", build.dir.string(), "--summary", (dir.path() / "s.md").string(), "--baseline", baseline.string()});

  CHECK(r.status == 2);
  CHECK_THAT(r.err, ContainsSubstring("cannot read " + baseline.string()));
}

namespace {

// A copy of fixture with a config file, configured.
testing::build configured_with(std::string_view fixture, std::string_view config_text, temporary_directory const& dir)
{
  auto const source = dir.path() / "source";
  fs::copy(testing::fixture(fixture), source, fs::copy_options::recursive);
  std::ofstream{source / ".cpp-deps-checker.yaml"} << config_text;
  return testing::configure(source, dir.path() / "build");
}

}  // namespace

TEST_CASE("check reads the config file in the source directory")
{
  temporary_directory const dir;
  // b includes a detail header of a, which only a test target may.
  auto const build = configured_with("entry_point_detail", "tests:\n  names: [b]\n", dir);

  auto const r = run({"check", build.dir.string()});

  CAPTURE(r.err);
  CHECK(r.status == 0);
  CHECK(r.out.empty());
}

TEST_CASE("graph reads the config file in the source directory")
{
  temporary_directory const dir;
  auto const build = configured_with("entry_point_detail", "tests:\n  names: [b]\n", dir);

  auto const r = run({"graph", build.dir.string()});

  CHECK(r.status == 0);
  CHECK_THAT(r.out, ContainsSubstring("[\"b\"]:::test"));
}

TEST_CASE("a config file that does not read is unusable input")
{
  temporary_directory const dir;
  auto const build = configured_with("minimal", "composition_root: [app]\n", dir);
  auto const command = GENERATE(std::string{"check"}, std::string{"graph"});
  CAPTURE(command);

  auto const r = run({command, build.dir.string()});

  CHECK(r.status == 2);
  CHECK_THAT(r.err, ContainsSubstring(".cpp-deps-checker.yaml"));
}

TEST_CASE("a composition root that names no production target is unusable input")
{
  temporary_directory const dir;
  auto const build = configured_with("minimal", "composition_roots: [missing]\n", dir);

  auto const r = run({"check", build.dir.string()});

  CHECK(r.status == 2);
  CHECK_THAT(r.err, ContainsSubstring(".cpp-deps-checker.yaml: composition_roots: no production target named missing"));
}
