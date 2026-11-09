#ifndef CPP_DEPS_CHECKER_OWNERSHIP_OWNERSHIP_HPP
#define CPP_DEPS_CHECKER_OWNERSHIP_OWNERSHIP_HPP

#include <cpp_deps_checker/model/model.hpp>

#include <filesystem>
#include <map>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <string_view>

namespace cpp_deps_checker::ownership {

struct owner {
  std::string target;  // id
  bool is_public = false;
  // A public header below a detail/ directory, counted from the directory that
  // gave the file its owner (file set base directory or source directory), so
  // that a target defined inside some detail/ directory does not make all its
  // headers detail headers.
  bool detail = false;

  friend bool operator==(owner const&, owner const&) = default;
};

namespace detail {

// A directory's claim on the files below it.
struct claim {
  std::string target;
  bool is_public = false;
};

}  // namespace detail

// Which target owns a file, by listed file, then file set base directory (for
// file sets that list no files), then source directory; within each directory
// tier the deepest directory wins. A nullopt entry marks a path claimed by
// several targets or by an external target.
class owners {
public:
  owners(
      std::map<std::filesystem::path, std::optional<owner>> listed, std::map<std::filesystem::path, std::optional<detail::claim>> base_directories,
      std::map<std::filesystem::path, std::optional<detail::claim>> source_directories
  );

  // Owner of an absolute, canonical path; nullopt if unowned or ambiguous.
  [[nodiscard]] std::optional<owner> of(std::filesystem::path const& path) const;

private:
  std::map<std::filesystem::path, std::optional<owner>> listed_;
  std::map<std::filesystem::path, std::optional<detail::claim>> base_directories_;
  std::map<std::filesystem::path, std::optional<detail::claim>> source_directories_;
};

// Ids of the targets whose code is not the project's: imported targets, and
// targets defined outside the top-level source directory or inside a build
// directory nested in it, where FetchContent puts what it fetches.
[[nodiscard]] std::set<std::string> find_external(model::project const& project);

[[nodiscard]] owners classify(model::project const& project);

inline constexpr std::string_view default_test_directories[]{"tests/**", "test/**"};

// Ids of the test targets: those defined by a CMakeLists.txt whose path,
// relative to the top-level source directory, matches one of directories (as
// Python's PurePath.full_match: ** spans directories, * stays within one), or
// whose name matches one of names (fnmatch patterns). External targets are
// never test targets, and a header set verification target is one exactly when
// the target it verifies is one.
[[nodiscard]] std::set<std::string> find_tests(
    model::project const& project, std::span<std::string_view const> directories = default_test_directories, std::span<std::string_view const> names = {}
);

}  // namespace cpp_deps_checker::ownership

#endif
