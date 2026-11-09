#include <cpp_deps_checker/includes/includes.hpp>
#include <cpp_deps_checker/rules/rules.hpp>
#include <cpp_deps_checker/tidy/cache.hpp>

#include <clang-tidy/ClangTidyCheck.h>
#include <clang-tidy/ClangTidyModule.h>
#include <clang/Basic/FileManager.h>
#include <clang/Lex/Preprocessor.h>

#include <format>
#include <memory>
#include <string_view>
#include <utility>

namespace cpp_deps_checker::tidy {

namespace {

// clang-tidy and clangd create the checks for each translation unit through a
// static registry, which leaves no composition root to own the cache that has
// to outlive them.
context_cache& contexts()
{
  static context_cache instance;
  return instance;
}

// Reports the violations of one include rule.
class include_check final : public ::clang::tidy::ClangTidyCheck {
public:
  include_check(::llvm::StringRef name, ::clang::tidy::ClangTidyContext* context, std::string_view rule) : ClangTidyCheck{name, context}, rule_{rule} {}

  void registerPPCallbacks(::clang::SourceManager const& sources, ::clang::Preprocessor* preprocessor, ::clang::Preprocessor*) override
  {
    // The compile command's directory: clang-tidy and clangd both run the
    // translation unit there.
    auto const working_dir = sources.getFileManager().getVirtualFileSystem().getCurrentWorkingDirectory();
    if (!working_dir) {
      return;
    }
    auto found = contexts().find(*working_dir);
    if (found.warning) {
      configurationDiag("cpp-deps-checker: %0") << *found.warning;
    }
    if (!found.context) {
      return;
    }
    preprocessor->addPPCallbacks(
        includes::recorder(sources, [this, &sources, context = std::move(found.context)](rules::include const& directive, ::clang::CharSourceRange filename) {
          for (auto const& violation : rules::check_include(*context, directive)) {
            if (violation.rule != rule_) {
              continue;
            }
            if (filename.isValid()) {
              diag(filename.getBegin(), "%0") << violation.message << filename;
            } else {
              diag(sources.getLocForStartOfFile(sources.getMainFileID()), "%0") << violation.message;
            }
          }
        })
    );
  }

private:
  std::string_view rule_;
};

class module final : public ::clang::tidy::ClangTidyModule {
public:
  void addCheckFactories(::clang::tidy::ClangTidyCheckFactories& factories) override
  {
    for (std::string_view const rule : {"direct-layering", "entry-point"}) {
      factories.registerCheckFactory(std::format("cpp-deps-{}", rule), [rule](::llvm::StringRef name, ::clang::tidy::ClangTidyContext* context) {
        return std::make_unique<include_check>(name, context, rule);
      });
    }
  }
};

::clang::tidy::ClangTidyModuleRegistry::Add<module> const registration{"cpp-deps-module", "Checks header includes against a CMake project's layering rules."};

}  // namespace

}  // namespace cpp_deps_checker::tidy
