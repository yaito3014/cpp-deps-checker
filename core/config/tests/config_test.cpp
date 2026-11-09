#include <cpp_deps_checker/config/config.hpp>
#include <cpp_deps_checker/testing/fixture.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <fstream>
#include <string>
#include <string_view>
#include <vector>

namespace {

namespace fs = std::filesystem;
using namespace cpp_deps_checker;
using Catch::Matchers::ContainsSubstring;
using testing::temporary_directory;

void write_config(fs::path const& dir, std::string_view text) { std::ofstream{dir / config::file_name} << text; }

}  // namespace

TEST_CASE("no config file is an empty config")
{
  temporary_directory const dir;

  auto const result = config::load(dir.path());

  REQUIRE(result);
  CHECK_FALSE(result->composition_roots);
  CHECK_FALSE(result->tests);
}

TEST_CASE("an empty config file is an empty config")
{
  temporary_directory const dir;
  write_config(dir.path(), "# nothing configured\n");

  auto const result = config::load(dir.path());

  REQUIRE(result);
  CHECK_FALSE(result->composition_roots);
  CHECK_FALSE(result->tests);
}

TEST_CASE("every key is read")
{
  temporary_directory const dir;
  write_config(
      dir.path(),
      "# the executables\n"
      "composition_roots: [app, tool]\n"
      "tests:\n"
      "  directories:\n"
      "    - \"**/tests/**\"\n"
      "  names: [\"*_test\"]\n"
  );

  auto const result = config::load(dir.path());

  REQUIRE(result);
  CHECK(result->composition_roots == std::vector<std::string>{"app", "tool"});
  REQUIRE(result->tests);
  CHECK(result->tests->directories == std::vector<std::string>{"**/tests/**"});
  CHECK(result->tests->names == std::vector<std::string>{"*_test"});
}

TEST_CASE("an omitted key stays unset")
{
  temporary_directory const dir;
  write_config(dir.path(), "tests:\n  names: [\"*_test\"]\n");

  auto const result = config::load(dir.path());

  REQUIRE(result);
  CHECK_FALSE(result->composition_roots);
  REQUIRE(result->tests);
  CHECK_FALSE(result->tests->directories);
}

TEST_CASE("an unknown key is an error naming the file and the position")
{
  temporary_directory const dir;
  write_config(dir.path(), "composition_root: [app]\n");

  auto const result = config::load(dir.path());

  REQUIRE_FALSE(result);
  CHECK_THAT(result.error(), ContainsSubstring((dir.path() / config::file_name).string()) && ContainsSubstring("1:"));
}

TEST_CASE("a value of the wrong type is an error")
{
  temporary_directory const dir;
  write_config(dir.path(), "tests:\n  directories: 3\n");

  CHECK_FALSE(config::load(dir.path()));
}

TEST_CASE("a config path that cannot be read is an error naming it")
{
  temporary_directory const dir;
  fs::create_directory(dir.path() / config::file_name);

  auto const result = config::load(dir.path());

  REQUIRE_FALSE(result);
  CHECK_THAT(result.error(), ContainsSubstring((dir.path() / config::file_name).string()));
}
