#ifndef CPP_DEPS_CHECKER_COLLECT_COLLECT_HPP
#define CPP_DEPS_CHECKER_COLLECT_COLLECT_HPP

#include <cpp_deps_checker/rules/rules.hpp>

#include <expected>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace cpp_deps_checker::collect {

using predicate = std::function<bool(std::filesystem::path const&)>;

struct collection {
  // Sorted, each once, and an edge without a line only if it has none with
  // one. A failed translation unit still contributes the includes it reached.
  std::vector<rules::include> includes;
  std::vector<rules::failure> failures;
};

// Each translation unit of the compilation database whose source skip does
// not accept, preprocessed in-process by Clang 24: the includes whose includer
// keep accepts, and the translation units that failed to preprocess. An
// unreadable database is an error.
[[nodiscard]] std::expected<collection, std::string> collect(std::filesystem::path const& compile_commands, predicate const& keep, predicate const& skip = {});

}  // namespace cpp_deps_checker::collect

#endif
