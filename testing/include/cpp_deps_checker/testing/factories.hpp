#ifndef CPP_DEPS_CHECKER_TESTING_FACTORIES_HPP
#define CPP_DEPS_CHECKER_TESTING_FACTORIES_HPP

#include <cpp_deps_checker/model/model.hpp>

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace cpp_deps_checker::testing {

// The top-level source directory of hand-built projects, and their default
// build directory, outside it.
inline std::filesystem::path const root{"/project"};
inline std::filesystem::path const default_build{"/build"};

struct target_options {
  std::string type = "INTERFACE_LIBRARY";
  bool imported = false;
  std::optional<std::filesystem::path> source_dir;  // root / name if not given
  std::vector<std::filesystem::path> sources;
  std::vector<model::file_set> file_sets;
  std::vector<std::string> links;  // interface links, by id
  std::optional<std::string> verifies;
};

// A target whose id is its name; paths are absolute.
[[nodiscard]] model::target target(std::string const& name, target_options options = {});

// A target defined in root / directory ("." for root itself) with the given
// interface links; options' source_dir and links are replaced.
[[nodiscard]] model::target placed(std::string const& name, std::string_view directory, std::vector<std::string> links = {}, target_options options = {});

// A HEADERS file set.
[[nodiscard]] model::file_set headers(
    std::string visibility, std::vector<std::filesystem::path> files, std::vector<std::filesystem::path> base_directories = {}
);

[[nodiscard]] model::project project(std::vector<model::target> targets, std::filesystem::path build_dir = default_build);

}  // namespace cpp_deps_checker::testing

#endif
