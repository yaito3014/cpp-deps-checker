#ifndef CPP_DEPS_CHECKER_TIDY_CACHE_HPP
#define CPP_DEPS_CHECKER_TIDY_CACHE_HPP

#include <cpp_deps_checker/rules/rules.hpp>

#include <filesystem>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <string>

namespace cpp_deps_checker::tidy {

struct lookup {
  std::shared_ptr<rules::context const> context;  // null without a usable reply
  // Why there is no context, only the first time for a build directory and
  // configure, or for a directory with no reply above it.
  std::optional<std::string> warning;
};

// The rule contexts of the CMake build directories above translation units'
// working directories, each loaded once per configure. Thread-safe, since
// clangd checks translation units in parallel.
class context_cache {
public:
  [[nodiscard]] lookup find(std::filesystem::path const& working_dir);

private:
  struct entry {
    std::string index;
    std::shared_ptr<rules::context const> context;
  };

  std::mutex mutex_;
  std::map<std::filesystem::path, entry> entries_;  // by build directory
  std::set<std::filesystem::path> without_reply_;   // working directories
};

}  // namespace cpp_deps_checker::tidy

#endif
