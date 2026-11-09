#include <cpp_deps_checker/model/model.hpp>
#include <cpp_deps_checker/testing/fixture.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <algorithm>
#include <format>
#include <fstream>
#include <ranges>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

namespace fs = std::filesystem;
using namespace cpp_deps_checker;
using model::load_error_kind;
using testing::temporary_directory;

model::project loaded(fs::path const& build_dir, std::optional<fs::path> const& trace)
{
  auto result = model::load(build_dir, trace);
  if (!result) {
    throw std::runtime_error{result.error().message};
  }
  return *std::move(result);
}

// Each fixture configured once for the whole run.
model::project const& configured(std::string_view fixture)
{
  static std::vector<std::pair<std::string, std::unique_ptr<temporary_directory>>> dirs;
  static std::vector<std::pair<std::string, model::project>> projects;
  for (auto const& [name, project] : projects) {
    if (name == fixture) {
      return project;
    }
  }
  auto& dir = dirs.emplace_back(std::string{fixture}, std::make_unique<temporary_directory>()).second;
  auto const build = testing::configure(testing::fixture(fixture), dir->path() / "build");
  return projects.emplace_back(std::string{fixture}, loaded(build.dir, build.trace)).second;
}

model::target const& named(model::project const& project, std::string_view name)
{
  auto const found = std::ranges::find(project.targets(), name, &model::target::name);
  REQUIRE(found != project.targets().end());
  return *found;
}

std::set<std::string> names(model::project const& project, std::vector<model::dependency> const& dependencies)
{
  std::set<std::string> result;
  for (auto const& dependency : dependencies) {
    auto const* target = project.find(dependency.target);
    REQUIRE(target != nullptr);
    result.insert(target->name);
  }
  return result;
}

fs::path canonical_fixture(std::string_view name) { return fs::canonical(testing::fixture(name)); }

}  // namespace

TEST_CASE("targets and abstract targets are loaded")
{
  auto const& basics = configured("model_basics");
  std::set<std::string> const declared{"a", "b", "c", "d", "e"};
  // Header set verification, which configure.sh enables, adds one target per
  // declared target and two aggregates.
  std::set<std::string> expected = declared;
  for (auto const& name : declared) {
    expected.insert(name + "_verify_interface_header_sets");
  }
  expected.insert({"all_verify_header_sets", "all_verify_interface_header_sets"});

  std::set<std::string> actual;
  for (auto const& target : basics.targets()) {
    actual.insert(target.name);
  }
  CHECK(actual == expected);
  CHECK(named(basics, "b").type == "INTERFACE_LIBRARY");
}

TEST_CASE("link libraries hold direct edges only")
{
  auto const& basics = configured("model_basics");
  auto const& a = named(basics, "a");
  CHECK(names(basics, a.link_libraries) == std::set<std::string>{"b", "c", "e"});
  CHECK(names(basics, a.compile_dependencies) == std::set<std::string>{"b", "c", "e"});
}

TEST_CASE("interface fields of a static library")
{
  auto const& basics = configured("model_basics");
  auto const& a = named(basics, "a");
  CHECK(names(basics, a.interface_link_libraries) == std::set<std::string>{"b", "c", "e"});
  CHECK(names(basics, a.interface_compile_dependencies) == std::set<std::string>{"c"});
}

TEST_CASE("interface library edges appear only in interface fields")
{
  auto const& basics = configured("model_basics");
  auto const& b = named(basics, "b");
  CHECK(b.link_libraries.empty());
  CHECK(b.compile_dependencies.empty());
  CHECK(names(basics, b.interface_link_libraries) == std::set<std::string>{"d"});
  CHECK(names(basics, b.interface_compile_dependencies) == std::set<std::string>{"d"});
}

TEST_CASE("declared dependencies join all four fields, each target once")
{
  auto const& basics = configured("model_basics");
  auto const in_order = [&](std::vector<model::dependency> const& dependencies) {
    std::vector<std::string> result;
    for (auto const& dependency : dependencies) {
      result.push_back(basics.find(dependency.target)->name);
    }
    return result;
  };
  CHECK(in_order(named(basics, "a").declared_dependencies()) == std::vector<std::string>{"b", "c", "e"});
  CHECK(in_order(named(basics, "b").declared_dependencies()) == std::vector<std::string>{"d"});
}

TEST_CASE("a dependency's location is the link call")
{
  auto const& basics = configured("model_basics");
  for (auto const& dependency : named(basics, "a").link_libraries) {
    CHECK(dependency.location == model::location{"CMakeLists.txt", 8});
  }
}

TEST_CASE("injected entries and fragments are not dependencies")
{
  auto const& project = configured("model_link_items");
  auto const& consumer = named(project, "consumer");
  CHECK(names(project, consumer.link_libraries) == std::set<std::string>{"injector"});
  CHECK(names(project, consumer.compile_dependencies) == std::set<std::string>{"injector"});
}

TEST_CASE("an error index newer than the index is stale")
{
  temporary_directory const dir;
  auto const build = testing::configure(testing::fixture("model_stale"), dir.path() / "build");
  std::vector<std::string> const fail{"-DFAIL=ON"};
  CHECK_THROWS(testing::configure(testing::fixture("model_stale"), build.dir, fail));

  auto const result = model::load(build.dir, build.trace);

  REQUIRE_FALSE(result);
  CHECK(result.error().kind == load_error_kind::stale);
}

namespace {

// A reply of minimal with an extra error index whose name sorts by timestamp.
std::expected<model::project, model::load_error> with_error_index(temporary_directory const& dir, std::string_view timestamp)
{
  auto const build = testing::configure(testing::fixture("minimal"), dir.path() / "build");
  std::ofstream{build.dir / ".cmake/api/v1/reply" / std::format("error-{}.json", timestamp)} << "{}";
  return model::load(build.dir, build.trace);
}

}  // namespace

TEST_CASE("an error index older than the index is not stale")
{
  temporary_directory const dir;
  CHECK(with_error_index(dir, "0000-01-01T00-00-00-0000"));
}

TEST_CASE("an error index newer than the index is stale, whatever the directory order")
{
  temporary_directory const dir;
  auto const result = with_error_index(dir, "9999-12-31T23-59-59-9999");
  REQUIRE_FALSE(result);
  CHECK(result.error().kind == load_error_kind::stale);
}

TEST_CASE("definitions follow the trace order of this project only")
{
  auto const& project = configured("model_definitions");
  auto const& first = named(project, "first").definition;
  auto const& second = named(project, "second").definition;
  auto const& third = named(project, "third").definition;
  REQUIRE(first);
  REQUIRE(second);
  REQUIRE(third);
  CHECK(first->order < second->order);
  CHECK(second->order < third->order);
  CHECK(first->location == model::location{"CMakeLists.txt", 6});
  CHECK(second->location == model::location{"CMakeLists.txt", 7});
  CHECK(third->location == model::location{"CMakeLists.txt", 9});
}

TEST_CASE("imported targets are marked")
{
  auto const& project = configured("model_definitions");
  CHECK(named(project, "ext::x").imported);
  CHECK_FALSE(named(project, "first").imported);
}

TEST_CASE("without a trace there are no definitions")
{
  temporary_directory const dir;
  auto const build = testing::configure(testing::fixture("model_definitions"), dir.path() / "build");

  auto const project = loaded(build.dir, std::nullopt);

  CHECK_FALSE(named(project, "first").definition);
}

TEST_CASE("top-level directories")
{
  temporary_directory const dir;
  auto const build = testing::configure(testing::fixture("minimal"), dir.path() / "build");

  auto const project = loaded(build.dir, build.trace);

  CHECK(project.source_dir() == canonical_fixture("minimal"));
  CHECK(project.build_dir() == fs::canonical(build.dir));
}

TEST_CASE("a missing reply is reported")
{
  temporary_directory const dir;

  auto const result = model::load(dir.path(), dir.path() / "trace.json");

  REQUIRE_FALSE(result);
  CHECK(result.error().kind == load_error_kind::missing);
  CHECK_THAT(result.error().message, Catch::Matchers::ContainsSubstring("no File API reply"));
}

TEST_CASE("a reply without a codemodel is reported")
{
  temporary_directory const dir;
  fs::create_directories(dir.path() / ".cmake/api/v1/query");
  std::ofstream{dir.path() / ".cmake/api/v1/query/cache-v2"};
  std::vector<std::string> const cmake{"cmake", "-S", testing::fixture("minimal").string(), "-B", dir.path().string(), "-G", "Ninja"};
  testing::run(cmake);

  auto const result = model::load(dir.path(), std::nullopt);

  REQUIRE_FALSE(result);
  CHECK(result.error().kind == load_error_kind::missing);
  CHECK_THAT(result.error().message, Catch::Matchers::ContainsSubstring("codemodel-v2"));
}

TEST_CASE("a multi-config generator is reported")
{
  temporary_directory const dir;
  std::vector<std::string> const generator{"-G", "Ninja Multi-Config"};
  testing::configure(testing::fixture("minimal"), dir.path(), generator);

  auto const result = model::load(dir.path(), std::nullopt);

  REQUIRE_FALSE(result);
  CHECK(result.error().kind == load_error_kind::unsupported);
  CHECK_THAT(result.error().message, Catch::Matchers::ContainsSubstring("multi-config"));
}

TEST_CASE("a missing trace is reported")
{
  temporary_directory const dir;
  auto const build = testing::configure(testing::fixture("minimal"), dir.path() / "build");

  auto const result = model::load(build.dir, dir.path() / "no-trace.json");

  REQUIRE_FALSE(result);
  CHECK(result.error().kind == load_error_kind::malformed);
}

TEST_CASE("a malformed reply is reported")
{
  temporary_directory const dir;
  auto const build = testing::configure(testing::fixture("minimal"), dir.path() / "build");
  for (auto const& entry : fs::directory_iterator{build.dir / ".cmake/api/v1/reply"}) {
    if (entry.path().filename().string().starts_with("codemodel-v2-")) {
      std::ofstream{entry.path()} << "{";
    }
  }

  auto const result = model::load(build.dir, build.trace);

  REQUIRE_FALSE(result);
  CHECK(result.error().kind == load_error_kind::malformed);
}

TEST_CASE("files of a built target")
{
  auto const& project = configured("direct_layering_public_private");
  auto const root = canonical_fixture("direct_layering_public_private");
  auto const& a = named(project, "a");

  CHECK(project.source_dir() == root);
  CHECK(a.source_dir == root / "a");
  CHECK(a.sources == std::vector{root / "a" / "a.cpp"});
  REQUIRE(a.file_sets.size() == 1);
  auto const& headers = a.file_sets.front();
  CHECK(headers.type == "HEADERS");
  CHECK(headers.visibility == "PUBLIC");
  CHECK(headers.base_directories == std::vector{root / "a" / "include"});
  CHECK(headers.files == std::vector{root / "a" / "include" / "a" / "a.hpp"});
}

TEST_CASE("an abstract interface library lists no files")
{
  auto const& project = configured("direct_layering_public_private");
  auto const root = canonical_fixture("direct_layering_public_private");
  auto const& c = named(project, "c");

  CHECK(c.sources.empty());
  REQUIRE(c.file_sets.size() == 1);
  auto const& headers = c.file_sets.front();
  CHECK(headers.visibility == "INTERFACE");
  CHECK(headers.base_directories == std::vector{root / "a" / "c" / "include"});
  CHECK(headers.files.empty());
}

TEST_CASE("locate is relative to the source directory")
{
  auto const& project = configured("direct_layering_public_private");
  auto const root = canonical_fixture("direct_layering_public_private");

  CHECK(project.locate(root / "a" / "a.cpp", 2) == model::location{"a/a.cpp", 2});
  CHECK(project.locate("/usr/include/x.h", std::nullopt) == model::location{"/usr/include/x.h", std::nullopt});
}

TEST_CASE("header set verification targets are marked")
{
  auto const& project = configured("direct_layering_public_private");
  auto const& a = named(project, "a");
  auto const& verification = named(project, "a_verify_interface_header_sets");

  // CMake creates it with a file-level backtrace only, so no definition.
  CHECK_FALSE(verification.definition);
  CHECK(verification.verifies == a.id);
  CHECK_FALSE(a.verifies);
}

TEST_CASE("header set verification targets are marked without a trace")
{
  temporary_directory const dir;
  auto const build = testing::configure(testing::fixture("direct_layering_public_private"), dir.path() / "build");

  auto const project = loaded(build.dir, std::nullopt);

  CHECK(named(project, "a_verify_interface_header_sets").verifies == named(project, "a").id);
}

TEST_CASE("the verified target of a verification source")
{
  auto const& project = configured("direct_layering_public_private");
  auto const& a = named(project, "a");
  auto const& verification = named(project, "a_verify_interface_header_sets");
  REQUIRE(verification.sources.size() == 1);

  CHECK(project.verified_target(verification.sources.front()) == &a);
  CHECK(project.verified_target(a.sources.front()) == nullptr);
}

TEST_CASE("a declared target with a verification name is not marked")
{
  auto const& project = configured("model_verification_name");
  CHECK_FALSE(named(project, "other_verify_interface_header_sets").verifies);
}

TEST_CASE("a file in two file sets of a target is in both")
{
  auto const& project = configured("model_file_in_two_sets");
  auto const header = canonical_fixture("model_file_in_two_sets") / "include" / "x" / "x.hpp";
  auto const& x = named(project, "x");

  REQUIRE(x.file_sets.size() == 2);
  CHECK(x.file_sets[0].visibility == "PUBLIC");
  CHECK(x.file_sets[0].files == std::vector{header});
  CHECK(x.file_sets[1].visibility == "PRIVATE");
  CHECK(x.file_sets[1].files == std::vector{header});
}

TEST_CASE("a location prints as file or file:line")
{
  CHECK(model::to_string(model::location{"a/CMakeLists.txt", 3}) == "a/CMakeLists.txt:3");
  CHECK(model::to_string(model::location{"/usr/include/x.h", std::nullopt}) == "/usr/include/x.h");
}
