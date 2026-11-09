#ifndef CPP_DEPS_CHECKER_MODEL_MODEL_HPP
#define CPP_DEPS_CHECKER_MODEL_MODEL_HPP

#include <cstddef>
#include <expected>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace cpp_deps_checker::model {

// As in the reply's backtraceGraph: relative to the top-level source directory
// when inside it, absolute otherwise.
struct location {
  std::filesystem::path file;
  std::optional<int> line;

  friend bool operator==(location const&, location const&) = default;
};

// file, or file:line.
[[nodiscard]] std::string to_string(location const& where);

struct dependency {
  std::string target;  // id of the target depended on
  std::optional<model::location> location;

  friend bool operator==(dependency const&, dependency const&) = default;
};

struct definition {
  std::size_t order;  // position in configure-time execution order; compare only
  model::location location;

  friend bool operator==(definition const&, definition const&) = default;
};

struct file_set {
  std::string name;
  std::string type;        // HEADERS, CXX_MODULES, ...
  std::string visibility;  // PUBLIC, PRIVATE, or INTERFACE
  std::vector<std::filesystem::path> base_directories;
  // Empty when the reply lists none, as for INTERFACE libraries that are not
  // part of the build system (abstractTargets).
  std::vector<std::filesystem::path> files;

  friend bool operator==(file_set const&, file_set const&) = default;
};

// A target and the dependencies its own target_link_libraries declares. Each
// dependency field mirrors the File API field of the same name, minus
// non-target fragments and entries injected by another target's
// INTERFACE_LINK_LIBRARIES_DIRECT (marked fromDependency).
struct target {
  std::string id;
  std::string name;
  std::string type;
  bool imported = false;
  // None without a trace, or if no add_library/add_executable call in the
  // trace matches, such as for targets CMake creates internally.
  std::optional<model::definition> definition;
  std::vector<dependency> link_libraries;
  std::vector<dependency> compile_dependencies;
  std::vector<dependency> interface_link_libraries;
  std::vector<dependency> interface_compile_dependencies;
  // Paths below are absolute and canonical.
  std::filesystem::path source_dir;            // directory whose CMakeLists.txt defines the target
  std::vector<std::filesystem::path> sources;  // sources in no file set
  std::vector<file_set> file_sets;
  // For a target CMake creates to verify another target's interface header
  // set (CMAKE_VERIFY_INTERFACE_HEADER_SETS): the id of that target.
  std::optional<std::string> verifies;

  // One dependency per target across all four fields, first one kept.
  [[nodiscard]] std::vector<dependency> declared_dependencies() const;

  friend bool operator==(target const&, target const&) = default;
};

namespace detail {

// Lets a std::string-keyed unordered_map be searched with a std::string_view.
struct transparent_hash {
  using is_transparent = void;
  [[nodiscard]] std::size_t operator()(std::string_view key) const noexcept { return std::hash<std::string_view>{}(key); }
};

}  // namespace detail

class project {
public:
  // source_dir and build_dir are the top-level ones, absolute and canonical.
  project(std::vector<target> targets, std::filesystem::path source_dir, std::filesystem::path build_dir);

  [[nodiscard]] std::vector<target> const& targets() const noexcept { return targets_; }  // in reply order
  [[nodiscard]] std::filesystem::path const& source_dir() const noexcept { return source_dir_; }
  [[nodiscard]] std::filesystem::path const& build_dir() const noexcept { return build_dir_; }

  [[nodiscard]] target const* find(std::string_view id) const noexcept;

  // Location of an absolute path, in the form the File API uses.
  [[nodiscard]] location locate(std::filesystem::path const& path, std::optional<int> line) const;

  // The target whose header set verification compiles source, if any.
  [[nodiscard]] target const* verified_target(std::filesystem::path const& source) const noexcept;

private:
  std::vector<target> targets_;
  std::filesystem::path source_dir_;
  std::filesystem::path build_dir_;
  std::unordered_map<std::string, std::size_t, detail::transparent_hash, std::equal_to<>> index_;  // id -> position in targets_
};

enum class load_error_kind {
  missing,      // no reply, or no codemodel-v2 in it
  stale,        // the last configure failed, so the reply describes an older run
  unsupported,  // a build the checker does not handle yet (multi-config)
  malformed,    // a reply or trace file that cannot be read or parsed
};

struct load_error {
  load_error_kind kind;
  std::string message;
};

// The File API reply in build_dir, with definitions ordered by the JSON trace
// if one is given.
[[nodiscard]] std::expected<project, load_error> load(std::filesystem::path const& build_dir, std::optional<std::filesystem::path> const& trace);

}  // namespace cpp_deps_checker::model

#endif
