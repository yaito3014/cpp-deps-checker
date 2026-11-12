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

// A violation the project allows. from, to, target, and targets hold target
// names; which of them an exception needs depends on its rule, and the rules
// check that. An omitted rule or reason is empty.
struct exception {
  std::string rule;
  std::optional<std::string> from;
  std::optional<std::string> to;
  std::optional<std::string> target;
  std::optional<std::vector<std::string>> targets;
  std::string reason;

  friend bool operator==(exception const&, exception const&) = default;
};

struct config {
  std::optional<std::vector<std::string>> composition_roots;  // target names
  std::optional<tests_config> tests;
  std::optional<std::vector<exception>> exceptions;
};

// The config file in source_dir; no file is an empty config.
[[nodiscard]] std::expected<config, std::string> load(std::filesystem::path const& source_dir);

}  // namespace cpp_deps_checker::config

#endif
