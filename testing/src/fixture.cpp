#include <cpp_deps_checker/testing/fixture.hpp>

#include <sys/wait.h>
#include <unistd.h>
#include <atomic>
#include <cstdlib>
#include <format>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <vector>

namespace cpp_deps_checker::testing {

namespace {

std::filesystem::path const source_dir{CPP_DEPS_CHECKER_SOURCE_DIR};

std::string shell_quoted(std::string_view argument)
{
  std::string result{"'"};
  for (char const c : argument) {
    result += c == '\'' ? std::string_view{R"('\'')"} : std::string_view{&c, 1};
  }
  return result + "'";
}

}  // namespace

std::filesystem::path fixture(std::string_view name) { return source_dir / "tests" / "fixtures" / name; }

temporary_directory::temporary_directory()
{
  static std::atomic<int> count{0};
  path_ = std::filesystem::temp_directory_path() / std::format("cpp-deps-checker-test-{}-{}", ::getpid(), count++);
  std::filesystem::remove_all(path_);
  std::filesystem::create_directories(path_);
}

temporary_directory::~temporary_directory()
{
  std::error_code ignored;
  std::filesystem::remove_all(path_, ignored);
}

void run(std::span<std::string const> argv)
{
  std::string command;
  for (auto const& argument : argv) {
    command += shell_quoted(argument) + ' ';
  }
  if (std::system(command.c_str()) != 0) {
    throw std::runtime_error{"command failed: " + command};
  }
}

process_result run_captured(
    std::span<std::string const> argv, std::filesystem::path const& directory, std::span<std::pair<std::string, std::string> const> environment
)
{
  temporary_directory const output;
  std::string command = "cd " + shell_quoted(directory.string()) + " && env";
  for (auto const& [name, value] : environment) {
    command += ' ' + shell_quoted(name + '=' + value);
  }
  for (auto const& argument : argv) {
    command += ' ' + shell_quoted(argument);
  }
  command += " >" + shell_quoted((output.path() / "out").string()) + " 2>" + shell_quoted((output.path() / "err").string());
  int const status = std::system(command.c_str());
  auto const read = [](std::filesystem::path const& file) {
    std::ifstream in{file};
    return std::string{std::istreambuf_iterator<char>{in}, {}};
  };
  return {WIFEXITED(status) ? WEXITSTATUS(status) : -1, read(output.path() / "out"), read(output.path() / "err")};
}

build configure(std::filesystem::path const& source_dir_to_configure, std::filesystem::path const& build_dir, std::span<std::string const> cmake_args)
{
  std::vector<std::string> argv{
      (source_dir / "action" / "configure.sh").string(), source_dir_to_configure.string(), build_dir.string(), "-G", "Ninja", "-DCMAKE_CXX_COMPILER=clang++",
  };
  argv.append_range(cmake_args);
  run(argv);
  return {.dir = build_dir, .trace = build_dir / "trace.json", .compile_commands = build_dir / "compile_commands.json"};
}

}  // namespace cpp_deps_checker::testing
