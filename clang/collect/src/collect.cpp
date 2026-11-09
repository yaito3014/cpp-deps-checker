#include <cpp_deps_checker/collect/collect.hpp>
#include <cpp_deps_checker/includes/includes.hpp>

#include <clang/Basic/Diagnostic.h>
#include <clang/Basic/DiagnosticOptions.h>
#include <clang/Frontend/ASTUnit.h>  // ClangTool destroys ASTUnits without defining them
#include <clang/Frontend/CompilerInstance.h>
#include <clang/Frontend/FrontendActions.h>
#include <clang/Frontend/TextDiagnosticPrinter.h>
#include <clang/Tooling/ArgumentsAdjusters.h>
#include <clang/Tooling/CompilationDatabase.h>
#include <clang/Tooling/JSONCompilationDatabase.h>
#include <clang/Tooling/Tooling.h>

#include <llvm/ADT/SmallString.h>
#include <llvm/Support/VirtualFileSystem.h>
#include <llvm/Support/raw_ostream.h>

#include <algorithm>
#include <format>
#include <optional>
#include <set>
#include <utility>

namespace cpp_deps_checker::collect {

namespace {

namespace fs = std::filesystem;

// Prints every diagnostic, as the compiler would, and records the first error.
class diagnostics final : public ::clang::DiagnosticConsumer {
public:
  void BeginSourceFile(::clang::LangOptions const& language, ::clang::Preprocessor const* preprocessor) override
  {
    printer_.BeginSourceFile(language, preprocessor);
  }

  void EndSourceFile() override { printer_.EndSourceFile(); }

  void HandleDiagnostic(::clang::DiagnosticsEngine::Level level, ::clang::Diagnostic const& info) override
  {
    DiagnosticConsumer::HandleDiagnostic(level, info);  // counts the errors
    printer_.HandleDiagnostic(level, info);
    if (level < ::clang::DiagnosticsEngine::Error || first_error_ || !info.hasSourceManager() || !info.getLocation().isValid()) {
      return;
    }
    auto const& sources = info.getSourceManager();
    auto const where = sources.getFileLoc(info.getLocation());
    auto const file = sources.getFileEntryRefForID(sources.getFileID(where));
    if (!file) {
      return;
    }
    ::llvm::SmallString<256> name{file->getName()};
    sources.getFileManager().makeAbsolutePath(name);
    ::llvm::SmallString<256> message;
    info.FormatDiagnostic(message);
    first_error_ = rules::diagnostic{fs::weakly_canonical(name.str().str()), static_cast<int>(sources.getSpellingLineNumber(where)), message.str().str()};
  }

  [[nodiscard]] std::optional<rules::diagnostic> const& first_error() const noexcept { return first_error_; }
  [[nodiscard]] std::string const& output() const noexcept { return output_; }

private:
  std::string output_;
  ::llvm::raw_string_ostream stream_{output_};
  ::clang::DiagnosticOptions options_;
  ::clang::TextDiagnosticPrinter printer_{stream_, options_};
  std::optional<rules::diagnostic> first_error_;
};

class recording_action final : public ::clang::PreprocessOnlyAction {
public:
  explicit recording_action(includes::sink on_include) : on_include_{std::move(on_include)} {}

private:
  bool BeginSourceFileAction(::clang::CompilerInstance& compiler) override
  {
    compiler.getPreprocessor().addPPCallbacks(includes::recorder(compiler.getSourceManager(), on_include_));
    return true;
  }

  includes::sink on_include_;
};

class recording_factory final : public ::clang::tooling::FrontendActionFactory {
public:
  explicit recording_factory(includes::sink on_include) : on_include_{std::move(on_include)} {}

  std::unique_ptr<::clang::FrontendAction> create() override { return std::make_unique<recording_action>(on_include_); }

private:
  includes::sink on_include_;
};

// CMake's module scanning, on by default with Ninja and C++20 or later, adds a
// module map response file to each compile command, which only a build writes.
// Once response files are expanded, an argument still naming one is missing,
// and a translation unit that imports no module does not need it.
[[nodiscard]] ::clang::tooling::CommandLineArguments without_missing_module_maps(::clang::tooling::CommandLineArguments arguments, ::llvm::StringRef)
{
  std::erase_if(arguments, [](std::string const& argument) { return argument.starts_with('@') && argument.ends_with(".modmap"); });
  return arguments;
}

}  // namespace

std::expected<collection, std::string> collect(std::filesystem::path const& compile_commands, predicate const& keep, predicate const& skip)
{
  std::string error;
  auto json = ::clang::tooling::JSONCompilationDatabase::loadFromFile(compile_commands.string(), error, ::clang::tooling::JSONCommandLineSyntax::AutoDetect);
  if (!json) {
    return std::unexpected{std::format("cannot read {}: {}", compile_commands.string(), error)};
  }
  auto const database = ::clang::tooling::expandResponseFiles(std::move(json), ::llvm::vfs::getRealFileSystem());

  std::set<rules::include> found;
  std::vector<rules::failure> failures;
  recording_factory factory{[&](rules::include directive, ::clang::CharSourceRange) {
    if (keep(directive.includer)) {
      found.insert(std::move(directive));
    }
  }};
  std::set<fs::path> seen;
  for (auto const& command : database->getAllCompileCommands()) {
    auto const file = (fs::path{command.Directory} / command.Filename).lexically_normal();
    auto const source = fs::weakly_canonical(file);
    if (!seen.insert(source).second || (skip && skip(source))) {
      continue;
    }
    ::clang::tooling::ClangTool tool{*database, {file.string()}};
    tool.setPrintErrorMessage(false);
    tool.appendArgumentsAdjuster(without_missing_module_maps);
    diagnostics consumer;
    tool.setDiagnosticConsumer(&consumer);
    if (tool.run(&factory) != 0 || consumer.getNumErrors() > 0) {
      failures.push_back({source, consumer.first_error(), consumer.output()});
    }
  }
  // A -include header that a directive also includes gives the same edge with
  // and without a line; the line is kept.
  std::set<std::pair<fs::path, fs::path>> located;
  for (auto const& directive : found) {
    if (directive.line) {
      located.emplace(directive.includer, directive.included);
    }
  }
  std::vector<rules::include> includes;
  for (auto const& directive : found) {
    if (directive.line || !located.contains({directive.includer, directive.included})) {
      includes.push_back(directive);
    }
  }
  return collection{std::move(includes), std::move(failures)};
}

}  // namespace cpp_deps_checker::collect
