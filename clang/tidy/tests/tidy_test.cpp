#include <cpp_deps_checker/testing/fixture.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators_range.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <glaze/glaze.hpp>

#include <algorithm>
#include <filesystem>
#include <format>
#include <fstream>
#include <ranges>
#include <regex>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

// Outside the unnamed namespace: glaze's reflection needs a type with linkage.
namespace cpp_deps_checker::tidy_test {

struct compile_command {
  std::string file;
};

}  // namespace cpp_deps_checker::tidy_test

namespace {

namespace fs = std::filesystem;
using namespace cpp_deps_checker;
using Catch::Matchers::ContainsSubstring;
using testing::temporary_directory;

using tidy_test::compile_command;

std::vector<std::string> sources(fs::path const& compile_commands)
{
  std::vector<compile_command> commands;
  std::string buffer;
  if (auto const ec = glz::read_file_json<glz::opts{.error_on_unknown_keys = false}>(commands, compile_commands.string(), buffer)) {
    throw std::runtime_error{glz::format_error(ec, buffer)};
  }
  return commands | std::views::transform(&compile_command::file) | std::ranges::to<std::vector>();
}

testing::process_result clang_tidy(fs::path const& build_dir, std::vector<std::string> const& files, std::string_view extra = "--header-filter=.*")
{
  std::vector<std::string> args{
      CPP_DEPS_CHECKER_CLANG_TIDY, "--load=" CPP_DEPS_CHECKER_TIDY_PLUGIN, "--checks=-*,cpp-deps-*", std::string{extra}, "-p", build_dir.string(),
  };
  args.append_range(files);
  return testing::run_captured(args, build_dir);
}

std::vector<std::string> lines(std::string const& text)
{
  return text | std::views::split('\n') | std::views::transform([](auto const& line) { return std::string{std::string_view{line}}; })
         | std::ranges::to<std::vector>();
}

// "file:line: message [rule]" for each diagnostic of the plugin's checks, the
// file relative to source_dir, as check prints them.
std::set<std::string> reported(std::string const& output, fs::path const& source_dir)
{
  static std::regex const diagnostic{R"re(^(.*):(\d+):\d+: warning: (.*) \[cpp-deps-([a-z-]+)\]$)re"};
  std::set<std::string> result;
  for (auto const& line : lines(output)) {
    if (std::smatch match; std::regex_match(line, match, diagnostic)) {
      auto const file = fs::weakly_canonical(match.str(1)).lexically_relative(source_dir);
      result.insert(std::format("{}:{}: {} [{}]", file.string(), match.str(2), match.str(3), match.str(4)));
    }
  }
  return result;
}

// The rule 3 and 4 lines of a fixture's expected.txt, in the form of reported.
std::set<std::string> expected(fs::path const& fixture)
{
  static std::regex const violation{R"re(^(.*:\d+): error: (.*) \[(direct-layering|entry-point)\]$)re"};
  std::ifstream in{fixture / "expected.txt"};
  std::set<std::string> result;
  for (std::string line; std::getline(in, line);) {
    if (std::smatch match; std::regex_match(line, match, violation)) {
      result.insert(std::format("{}: {} [{}]", match.str(1), match.str(2), match.str(3)));
    }
  }
  return result;
}

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

TEST_CASE("the plugin registers a check for each include rule")
{
  temporary_directory const dir;

  auto const result = clang_tidy(dir.path(), {}, "--list-checks");

  CAPTURE(result.err);
  CHECK(result.status == 0);
  CHECK_THAT(result.out, ContainsSubstring("cpp-deps-direct-layering") && ContainsSubstring("cpp-deps-entry-point"));
}

// Every translation unit of the fixture, headers included, so that each
// include check reports is reached.
TEST_CASE("clang-tidy reports what check reports for rules 3 and 4")
{
  auto const fixture = GENERATE(from_range(fixtures_with_expected_output()));
  CAPTURE(fixture);
  temporary_directory const dir;
  auto const build = testing::configure(testing::fixture(fixture), dir.path() / "build");
  // A project without languages has no translation units.
  if (!fs::exists(build.compile_commands)) {
    CHECK(expected(testing::fixture(fixture)).empty());
    return;
  }

  auto const result = clang_tidy(build.dir, sources(build.compile_commands));

  CAPTURE(result.out, result.err);
  CHECK(reported(result.out, fs::canonical(testing::fixture(fixture))) == expected(testing::fixture(fixture)));
}

TEST_CASE("the include rules have fixtures")
{
  auto const with_include_violations =
      std::ranges::count_if(fixtures_with_expected_output(), [](auto const& name) { return !expected(testing::fixture(name)).empty(); });
  CHECK(with_include_violations >= 4);
}

TEST_CASE("a header given with -include is reported at the start of the main file")
{
  temporary_directory const dir;
  auto const source = dir.path() / "source";
  fs::create_directories(source / "c" / "include" / "c");
  std::ofstream{source / "c" / "include" / "c" / "c.hpp"} << "#pragma once\n";
  std::ofstream{source / "a.cpp"} << "int a() { return 0; }\n";
  std::ofstream{source / "CMakeLists.txt"} << "cmake_minimum_required(VERSION 4.2)\n"
                                              "project(p LANGUAGES CXX)\n"
                                              "add_library(c INTERFACE)\n"
                                              "target_sources(c INTERFACE FILE_SET HEADERS BASE_DIRS c/include FILES c/include/c/c.hpp)\n"
                                              "add_library(a STATIC a.cpp)\n"
                                              "target_include_directories(a PRIVATE c/include)\n"
                                              "target_compile_options(a PRIVATE -include c/c.hpp)\n";
  auto const build = testing::configure(source, dir.path() / "build");

  auto const result = clang_tidy(build.dir, {(source / "a.cpp").string()});

  CAPTURE(result.out, result.err);
  CHECK(
      reported(result.out, fs::canonical(source))
      == std::set<std::string>{"a.cpp:1: a includes c/include/c/c.hpp of c without linking c directly [direct-layering]"}
  );
}

TEST_CASE("a directory without a File API reply is reported once")
{
  temporary_directory const dir;
  std::ofstream{dir.path() / "a.cpp"} << "int a() { return 0; }\n";
  std::ofstream{dir.path() / "b.cpp"} << "int b() { return 0; }\n";
  std::ofstream{dir.path() / "compile_commands.json"} << std::format(
      R"([{{"directory": "{0}", "file": "{0}/a.cpp", "command": "clang++ -c a.cpp"}},)"
      R"( {{"directory": "{0}", "file": "{0}/b.cpp", "command": "clang++ -c b.cpp"}}])",
      dir.path().string()
  );

  auto const result = clang_tidy(dir.path(), {"a.cpp", "b.cpp"});

  CAPTURE(result.out, result.err);
  auto const warnings = std::ranges::count_if(lines(result.out + result.err), [](std::string const& line) {
    return line.contains("cpp-deps-checker: no CMake File API reply in");
  });
  CHECK(warnings == 1);
}

TEST_CASE("clangd reports the include rules of the file it opens")
{
  temporary_directory const dir;
  auto const fixture = testing::fixture("direct_layering_transitive");
  auto const build = testing::configure(fixture, dir.path() / "build");
  fs::create_directories(dir.path() / "config" / "clangd");
  // clangd runs only the checks it knows to be fast unless told otherwise.
  std::ofstream{dir.path() / "config" / "clangd" / "config.yaml"} << "Diagnostics:\n"
                                                                     "  ClangTidy:\n"
                                                                     "    Add: cpp-deps-*\n"
                                                                     "    FastCheckFilter: Loose\n";
  std::vector<std::string> const args{
      CPP_DEPS_CHECKER_CLANGD,
      "--load=" CPP_DEPS_CHECKER_TIDY_PLUGIN,
      "--compile-commands-dir=" + build.dir.string(),
      "--check=" + (fixture / "a" / "a.cpp").string(),
      "--check-warnings",
  };
  std::vector<std::pair<std::string, std::string>> const environment{{"XDG_CONFIG_HOME", (dir.path() / "config").string()}};

  auto const result = testing::run_captured(args, dir.path(), environment);

  CAPTURE(result.out, result.err);
  CHECK_THAT(result.err, ContainsSubstring("[cpp-deps-direct-layering] Line 2: a includes a/b/c/include/c/c.hpp of c without linking c directly"));
}
