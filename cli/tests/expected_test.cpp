#include <cpp_deps_checker/cli/cli.hpp>
#include <cpp_deps_checker/testing/fixture.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators_range.hpp>

#include <algorithm>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace {

namespace fs = std::filesystem;
using namespace cpp_deps_checker;

constexpr std::string_view status_prefix = "# exit status: ";

std::vector<std::string> fixtures_with_expected_output()
{
  std::vector<std::string> names;
  for (auto const& entry : fs::directory_iterator{testing::fixture("")}) {
    if (fs::exists(entry.path() / "expected.txt")) {
      names.push_back(entry.path().filename().string());
    }
  }
  std::ranges::sort(names);
  return names;
}

}  // namespace

// The fixtures' expected.txt, which the Python prototype passes: parity of the
// exit status and of the standard output of check.
TEST_CASE("check matches each fixture's expected output")
{
  auto const fixture = GENERATE(from_range(fixtures_with_expected_output()));
  CAPTURE(fixture);
  std::ifstream in{testing::fixture(fixture) / "expected.txt"};
  std::string status_line;
  std::getline(in, status_line);
  REQUIRE(status_line.starts_with(status_prefix));
  std::string const expected{std::istreambuf_iterator<char>{in}, {}};
  testing::temporary_directory const dir;
  auto const build = testing::configure(testing::fixture(fixture), dir.path() / "build");

  std::vector<std::string> const args{"cpp-deps-checker", "check", build.dir.string()};
  std::ostringstream out;
  std::ostringstream err;
  int const status = cli::run(args, out, err);

  CAPTURE(err.str());
  CHECK(status == std::stoi(status_line.substr(status_prefix.size())));
  CHECK(out.str() == expected);
}

TEST_CASE("there are fixtures with expected output") { CHECK(fixtures_with_expected_output().size() >= 20); }
