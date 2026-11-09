#include <cpp_deps_checker/includes/includes.hpp>

#include <clang/Basic/FileManager.h>

#include <llvm/ADT/SmallString.h>

#include <filesystem>
#include <optional>
#include <utility>

namespace cpp_deps_checker::includes {

namespace {

namespace fs = std::filesystem;

class recording_callbacks final : public ::clang::PPCallbacks {
public:
  recording_callbacks(::clang::SourceManager const& sources, sink on_include) : sources_{sources}, on_include_{std::move(on_include)} {}

  void InclusionDirective(
      ::clang::SourceLocation hash, ::clang::Token const&, ::llvm::StringRef, bool, ::clang::CharSourceRange filename, ::clang::OptionalFileEntryRef file,
      ::llvm::StringRef, ::llvm::StringRef, ::clang::Module const*, bool, ::clang::SrcMgr::CharacteristicKind
  ) override
  {
    if (!file) {
      return;  // not found: the compiler reports it
    }
    auto const includer = sources_.getFileEntryRefForID(sources_.getFileID(hash));
    if (includer) {
      // The spelling line counts the file's own lines, which #line does not
      // renumber.
      on_include_({path_of(*includer), path_of(*file), static_cast<int>(sources_.getSpellingLineNumber(hash))}, filename);
    } else if (auto const main = sources_.getFileEntryRefForID(sources_.getMainFileID())) {
      // In the predefines buffer: a header given with -include.
      on_include_({path_of(*main), path_of(*file), std::nullopt}, {});
    }
  }

private:
  [[nodiscard]] fs::path path_of(::clang::FileEntryRef file) const
  {
    ::llvm::SmallString<256> name{file.getName()};
    sources_.getFileManager().makeAbsolutePath(name);
    return fs::weakly_canonical(fs::path{name.str().str()});
  }

  ::clang::SourceManager const& sources_;
  sink on_include_;
};

}  // namespace

std::unique_ptr<::clang::PPCallbacks> recorder(::clang::SourceManager const& sources, sink on_include)
{
  return std::make_unique<recording_callbacks>(sources, std::move(on_include));
}

}  // namespace cpp_deps_checker::includes
