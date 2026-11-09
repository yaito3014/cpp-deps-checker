#include <cpp_deps_checker/testing/fixture.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <cstdlib>
#include <fstream>
#include <ranges>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

namespace fs = std::filesystem;
using namespace cpp_deps_checker;
using Catch::Matchers::EndsWith;
using Catch::Matchers::StartsWith;
using testing::temporary_directory;

using environment = std::vector<std::pair<std::string, std::string>>;

// Isolated from the user's git configuration (hooks, signing, identity).
environment const git_environment{
    {"GIT_CONFIG_GLOBAL", "/dev/null"},       {"GIT_CONFIG_NOSYSTEM", "1"},   {"GIT_AUTHOR_NAME", "test"},
    {"GIT_AUTHOR_EMAIL", "test@example.com"}, {"GIT_COMMITTER_NAME", "test"}, {"GIT_COMMITTER_EMAIL", "test@example.com"},
};

std::string git(fs::path const& directory, std::vector<std::string> args)
{
  args.insert(args.begin(), "git");
  auto const result = testing::run_captured(args, directory, git_environment);
  if (result.status != 0) {
    throw std::runtime_error{"git failed: " + result.err};
  }
  return result.out;
}

std::string read(fs::path const& file)
{
  std::ifstream in{file};
  return {std::istreambuf_iterator<char>{in}, {}};
}

// A clone, as actions/checkout makes one, of a branch that replaces the
// project under proj/ on main with the head fixture.
fs::path checkout(fs::path const& dir, std::string_view base, std::string_view head, bool shallow)
{
  auto const origin = dir / "origin.git";
  git(dir, {"init", "--quiet", "--bare", "--initial-branch=main", origin.string()});
  auto const author = dir / "author";
  git(dir, {"clone", "--quiet", origin.string(), author.string()});
  fs::copy(testing::fixture(base), author / "proj", fs::copy_options::recursive);
  git(author, {"add", "--all"});
  git(author, {"commit", "--quiet", "--message=base"});
  git(author, {"push", "--quiet", "origin", "HEAD:main"});
  fs::remove_all(author / "proj");
  fs::copy(testing::fixture(head), author / "proj", fs::copy_options::recursive);
  git(author, {"add", "--all"});
  git(author, {"commit", "--quiet", "--message=head"});
  git(author, {"push", "--quiet", "origin", "HEAD:feature"});
  auto const result = dir / "checkout";
  std::vector<std::string> clone{"clone", "--quiet", "--branch=feature", "file://" + origin.string(), result.string()};
  if (shallow) {
    clone.insert(clone.begin() + 2, "--depth=1");
  }
  git(dir, clone);
  return result;
}

testing::process_result run_action(fs::path const& checkout_dir, fs::path const& work, std::string const& base_ref)
{
  auto env = git_environment;
  env.emplace_back("PATH", fs::path{CPP_DEPS_CHECKER_EXECUTABLE}.parent_path().string() + ":" + std::getenv("PATH"));
  env.emplace_back("SOURCE_DIR", "proj");
  env.emplace_back("CMAKE_ARGS", "-G Ninja -DCMAKE_CXX_COMPILER=clang++ -DACTION_ARGS_PASSED=ON");
  env.emplace_back("BASE_REF", base_ref);
  env.emplace_back("WORK_DIR", work.string());
  env.emplace_back("SUMMARY_FILE", (work.parent_path() / "summary.md").string());
  std::vector<std::string> const run{(fs::path{CPP_DEPS_CHECKER_SOURCE_DIR} / "action" / "run.sh").string()};
  return testing::run_captured(run, checkout_dir, env);
}

std::vector<std::string> lines_starting_with(std::string const& text, std::string_view prefix)
{
  std::vector<std::string> result;
  for (auto const line : text | std::views::split('\n')) {
    std::string_view const view{line};
    if (view.starts_with(prefix)) {
      result.emplace_back(view);
    }
  }
  return result;
}

}  // namespace

TEST_CASE("the Action lists the new edges against the base branch")
{
  auto const shallow = GENERATE(true, false);
  CAPTURE(shallow);
  temporary_directory const dir;
  auto const checkout_dir = checkout(dir.path(), "action_base", "definition_order_late", shallow);
  auto const work = dir.path() / "work";

  auto const result = run_action(checkout_dir, work, "main");

  CAPTURE(result.err);
  CHECK(result.status == 1);
  CHECK(
      lines_starting_with(result.out, "::error ")
      == std::vector<std::string>{
          "::error file=proj/CMakeLists.txt,line=5,title=definition-order::a links late, which is defined later at late/CMakeLists.txt:1; define late "
          "before a"
      }
  );
  CHECK_THAT(read(work / "graph.md"), StartsWith("```mermaid\n"));
  CHECK_THAT(read(dir.path() / "summary.md"), EndsWith("### New edges\n\n- `a` -> `late`\n"));
  CHECK(lines_starting_with(git(checkout_dir, {"worktree", "list", "--porcelain"}), "worktree ").size() == 1);
  CHECK(git(checkout_dir, {"rev-parse", "--is-shallow-repository"}) == (shallow ? "true\n" : "false\n"));
}

TEST_CASE("a base that does not configure skips the new edges")
{
  temporary_directory const dir;
  auto const checkout_dir = checkout(dir.path(), "action_broken", "definition_order_imported", true);

  auto const result = run_action(checkout_dir, dir.path() / "work", "main");

  CAPTURE(result.err);
  CHECK(result.status == 0);
  CHECK(
      lines_starting_with(result.out, "::warning")
      == std::vector<std::string>{"::warning title=cpp-deps-checker::could not configure main; new edges are not listed"}
  );
  CHECK_THAT(read(dir.path() / "summary.md"), EndsWith("new edges are not listed.\n"));
}

TEST_CASE("no base ref skips the new edges")
{
  temporary_directory const dir;
  auto const checkout_dir = checkout(dir.path(), "action_base", "definition_order_imported", true);

  auto const result = run_action(checkout_dir, dir.path() / "work", "");

  CAPTURE(result.err);
  CHECK(result.status == 0);
  CHECK(lines_starting_with(result.out, "::warning").empty());
  CHECK_THAT(read(dir.path() / "summary.md"), EndsWith("new edges are not listed.\n"));
}
