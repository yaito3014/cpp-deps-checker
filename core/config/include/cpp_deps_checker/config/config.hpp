#ifndef CPP_DEPS_CHECKER_CONFIG_CONFIG_HPP
#define CPP_DEPS_CHECKER_CONFIG_CONFIG_HPP

#include <expected>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace cpp_deps_checker::config {

// In the top-level source directory.
inline constexpr std::string_view file_name = ".cpp-deps-checker.yaml";

// What the file says; a key it omits is nullopt, and its default is up to the
// rule it configures.
struct tests_config {
  std::optional<std::vector<std::string>> directories;  // globs of defining CMakeLists.txt paths
  std::optional<std::vector<std::string>> names;        // target name patterns
};

struct config {
  std::optional<std::vector<std::string>> composition_roots;  // target names
  std::optional<tests_config> tests;
};

// The config file in source_dir; no file is an empty config.
[[nodiscard]] std::expected<config, std::string> load(std::filesystem::path const& source_dir);

}  // namespace cpp_deps_checker::config

#endif
