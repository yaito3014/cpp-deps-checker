#include <cpp_deps_checker/testing/fixture.hpp>
#include <cpp_deps_checker/tidy/cache.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <filesystem>
#include <fstream>

namespace {

namespace fs = std::filesystem;
using namespace cpp_deps_checker;
using Catch::Matchers::ContainsSubstring;
using testing::temporary_directory;

fs::path reply_dir(testing::build const& build) { return build.dir / ".cmake/api/v1/reply"; }

}  // namespace

TEST_CASE("a build directory's context is loaded once per configure")
{
  temporary_directory const dir;
  auto const build = testing::configure(testing::fixture("direct_layering_transitive"), dir.path() / "build");
  fs::create_directories(build.dir / "sub");
  tidy::context_cache cache;

  auto const first = cache.find(build.dir / "sub");
  auto const again = cache.find(build.dir);

  REQUIRE(first.context);
  CHECK_FALSE(first.warning);
  CHECK_FALSE(first.context->project.targets().empty());
  CHECK_FALSE(first.context->owners.of(fs::canonical(testing::fixture("direct_layering_transitive") / "a/a.cpp")) == std::nullopt);
  CHECK(again.context == first.context);

  // What a configure writes, under a newer name.
  for (auto const& entry : fs::directory_iterator{reply_dir(build)}) {
    if (entry.path().filename().string().starts_with("index-")) {
      fs::copy_file(entry.path(), reply_dir(build) / "index-9999-12-31T23-59-59-9999.json");
    }
  }
  auto const reconfigured = cache.find(build.dir);

  REQUIRE(reconfigured.context);
  CHECK(reconfigured.context != first.context);
}

TEST_CASE("a failed configure warns once per configure")
{
  temporary_directory const dir;
  auto const build = testing::configure(testing::fixture("minimal"), dir.path() / "build");
  std::ofstream{reply_dir(build) / "error-9999-12-31T23-59-59-9999.json"} << "{}";
  tidy::context_cache cache;

  auto const first = cache.find(build.dir);
  auto const again = cache.find(build.dir);
  std::ofstream{reply_dir(build) / "error-9999-12-31T23-59-59-99999.json"} << "{}";
  auto const failed_again = cache.find(build.dir);

  CHECK_FALSE(first.context);
  REQUIRE(first.warning);
  CHECK_THAT(*first.warning, ContainsSubstring(build.dir.string()) && ContainsSubstring("the last configure failed"));
  CHECK_FALSE(again.context);
  CHECK_FALSE(again.warning);
  CHECK_FALSE(failed_again.context);
  CHECK(failed_again.warning);
}

TEST_CASE("a directory without a reply above it warns once")
{
  temporary_directory const dir;
  tidy::context_cache cache;

  auto const first = cache.find(dir.path());
  auto const again = cache.find(dir.path());

  CHECK_FALSE(first.context);
  REQUIRE(first.warning);
  CHECK_THAT(*first.warning, ContainsSubstring("no CMake File API reply in " + dir.path().string()));
  CHECK_FALSE(again.context);
  CHECK_FALSE(again.warning);
}

namespace {

// A configured copy of fixture, whose config file a test can change.
testing::build configured_copy(std::string_view fixture, temporary_directory const& dir)
{
  fs::copy(testing::fixture(fixture), dir.path() / "source", fs::copy_options::recursive);
  return testing::configure(dir.path() / "source", dir.path() / "build");
}

}  // namespace

TEST_CASE("a changed config file reloads the context")
{
  temporary_directory const dir;
  auto const build = configured_copy("entry_point_detail", dir);
  tidy::context_cache cache;
  auto const before = cache.find(build.dir);
  std::ofstream{dir.path() / "source" / ".cpp-deps-checker.yaml"} << "tests:\n  names: [b]\n";

  auto const after = cache.find(build.dir);

  REQUIRE(before.context);
  REQUIRE(after.context);
  CHECK(before.context->tests.empty());
  CHECK(after.context->tests.size() == 1);
}

TEST_CASE("a config file that does not read warns once")
{
  temporary_directory const dir;
  auto const build = configured_copy("minimal", dir);
  std::ofstream{dir.path() / "source" / ".cpp-deps-checker.yaml"} << "composition_root: [app]\n";
  tidy::context_cache cache;

  auto const first = cache.find(build.dir);
  auto const again = cache.find(build.dir);

  CHECK_FALSE(first.context);
  REQUIRE(first.warning);
  CHECK_THAT(*first.warning, ContainsSubstring(".cpp-deps-checker.yaml"));
  CHECK_FALSE(again.context);
  CHECK_FALSE(again.warning);
}
