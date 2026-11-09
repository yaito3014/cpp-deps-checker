#ifndef CPP_DEPS_CHECKER_INCLUDES_INCLUDES_HPP
#define CPP_DEPS_CHECKER_INCLUDES_INCLUDES_HPP

#include <cpp_deps_checker/rules/rules.hpp>

#include <clang/Basic/SourceLocation.h>
#include <clang/Basic/SourceManager.h>
#include <clang/Lex/PPCallbacks.h>

#include <functional>
#include <memory>

namespace cpp_deps_checker::includes {

// An include directive, and the range of its file name as spelled; the range
// is invalid for a header given with -include.
using sink = std::function<void(rules::include, ::clang::CharSourceRange)>;

// Preprocessor callbacks that pass on_include each include directive the
// preprocessor runs and finds a file for, a header its include guard skips
// included. The line is the directive's physical line, which #line does not
// change. A header given with -include counts as included by the main file,
// without a line. Paths are absolute and canonical.
[[nodiscard]] std::unique_ptr<::clang::PPCallbacks> recorder(::clang::SourceManager const& sources, sink on_include);

}  // namespace cpp_deps_checker::includes

#endif
