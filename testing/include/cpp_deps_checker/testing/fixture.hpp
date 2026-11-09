#ifndef CPP_DEPS_CHECKER_TESTING_FIXTURE_HPP
#define CPP_DEPS_CHECKER_TESTING_FIXTURE_HPP

#include <filesystem>
#include <span>
#include <string>
#include <string_view>

namespace cpp_deps_checker::testing {

// The fixture CMake project tests/fixtures/<name>, absolute.
[[nodiscard]] std::filesystem::path fixture(std::string_view name);

// A new empty directory, removed with everything in it on destruction.
class temporary_directory {
public:
  temporary_directory();
  ~temporary_directory();
  temporary_directory(temporary_directory const&) = delete;
  temporary_directory& operator=(temporary_directory const&) = delete;

  [[nodiscard]] std::filesystem::path const& path() const noexcept { return path_; }

private:
  std::filesystem::path path_;
};

// Runs argv through the shell, each argument quoted; throws std::runtime_error
// if the command does not exit with status 0.
void run(std::span<std::string const> argv);

struct build {
  std::filesystem::path dir;
  std::filesystem::path trace;
  std::filesystem::path compile_commands;
};

// Configures source_dir into build_dir through action/configure.sh, the way the
// Action configures a consumer project: Ninja and clang++, then cmake_args.
build configure(std::filesystem::path const& source_dir, std::filesystem::path const& build_dir, std::span<std::string const> cmake_args = {});

}  // namespace cpp_deps_checker::testing

#endif
