#include <cpp_deps_checker/ownership/ownership.hpp>

#include <algorithm>
#include <ranges>
#include <utility>
#include <vector>

namespace cpp_deps_checker::ownership {

namespace {

namespace fs = std::filesystem;
using detail::claim;

[[nodiscard]] bool is_within(fs::path const& path, fs::path const& directory)
{
  auto const [rest, _] = std::ranges::mismatch(directory, path);
  return rest == directory.end();
}

[[nodiscard]] owner owner_of(claim const& c, std::optional<fs::path> const& relative)
{
  bool const detail = c.is_public && relative && std::ranges::contains(relative->parent_path(), fs::path{"detail"});
  return {c.target, c.is_public, detail};
}

// path below the deepest of directories that contains it.
[[nodiscard]] std::optional<fs::path> relative_to_deepest(fs::path const& path, std::span<fs::path const> directories)
{
  std::optional<fs::path> deepest;
  for (auto const& directory : directories) {
    if (is_within(path, directory) && (!deepest || std::ranges::distance(directory) > std::ranges::distance(*deepest))) {
      deepest = directory;
    }
  }
  return deepest.transform([&](fs::path const& directory) { return path.lexically_relative(directory); });
}

[[nodiscard]] bool is_public(model::target const& target, std::string_view visibility)
{
  // An INTERFACE library has no sources of its own: everything it owns is
  // there for its consumers.
  return target.type == "INTERFACE_LIBRARY" || visibility == "PUBLIC" || visibility == "INTERFACE";
}

[[nodiscard]] claim merged(std::span<claim const> claims) { return {claims.front().target, std::ranges::any_of(claims, &claim::is_public)}; }

// Public if any file set exposes the file; a detail header only if every file
// set that exposes it puts it below a detail/ directory, since one that does
// not makes it reachable as an entry point.
[[nodiscard]] owner merged(std::span<owner const> owners)
{
  auto exposing = owners | std::views::filter(&owner::is_public);
  bool const any_public = !std::ranges::empty(exposing);
  return {owners.front().target, any_public, any_public && std::ranges::all_of(exposing, &owner::detail)};
}

// Each path's claims merged into one, or nullopt if they come from several
// targets or one is nullopt. A target claims a file several times when the
// file is in several of its file sets, which CMake allows.
template<class T>
[[nodiscard]] std::map<fs::path, std::optional<T>> indexed(std::vector<std::pair<fs::path, std::optional<T>>> const& claims)
{
  std::map<fs::path, std::vector<std::optional<T>>> found;
  for (auto const& [path, c] : claims) {
    found[path].push_back(c);
  }
  std::map<fs::path, std::optional<T>> result;
  for (auto const& [path, all] : found) {
    std::vector<T> known;
    for (auto const& c : all) {
      if (c) {
        known.push_back(*c);
      }
    }
    bool const one_target = known.size() == all.size() && std::ranges::all_of(known, [&](T const& c) { return c.target == known.front().target; });
    result.emplace(path, one_target ? std::optional<T>{merged(std::span<T const>{known})} : std::nullopt);
  }
  return result;
}

[[nodiscard]] std::optional<fs::path> nested_build_dir(model::project const& project)
{
  auto const& source = project.source_dir();
  auto const& build = project.build_dir();
  return build != source && is_within(build, source) ? std::optional{build} : std::nullopt;
}

// One segment of a glob, as fnmatch: * and ? within the segment, [...] and
// [!...] character classes.
[[nodiscard]] bool segment_matches(std::string_view pattern, std::string_view text)
{
  if (pattern.empty()) {
    return text.empty();
  }
  switch (pattern.front()) {
    case '*':
      for (std::size_t skip = 0; skip <= text.size(); ++skip) {
        if (segment_matches(pattern.substr(1), text.substr(skip))) {
          return true;
        }
      }
      return false;
    case '?':
      return !text.empty() && segment_matches(pattern.substr(1), text.substr(1));
    case '[': {
      auto const close = pattern.find(']', pattern.size() > 2 && pattern[1] == '!' ? 3 : 2);
      if (close == std::string_view::npos) {
        break;  // no class: a literal [
      }
      if (text.empty()) {
        return false;
      }
      bool const negated = pattern[1] == '!';
      auto const set = pattern.substr(negated ? 2 : 1, close - (negated ? 2 : 1));
      bool in_set = false;
      for (std::size_t i = 0; i < set.size(); ++i) {
        if (i + 2 < set.size() && set[i + 1] == '-') {
          in_set = in_set || (set[i] <= text.front() && text.front() <= set[i + 2]);
          i += 2;
        } else {
          in_set = in_set || set[i] == text.front();
        }
      }
      return in_set != negated && segment_matches(pattern.substr(close + 1), text.substr(1));
    }
    default:
      break;
  }
  return !text.empty() && pattern.front() == text.front() && segment_matches(pattern.substr(1), text.substr(1));
}

// The whole of path against pattern, segment by segment, ** spanning any
// number of segments, as PurePath.full_match.
[[nodiscard]] bool full_match(std::span<std::string const> path, std::span<std::string_view const> pattern)
{
  if (pattern.empty()) {
    return path.empty();
  }
  if (pattern.front() == "**") {
    for (std::size_t skip = 0; skip <= path.size(); ++skip) {
      if (full_match(path.subspan(skip), pattern.subspan(1))) {
        return true;
      }
    }
    return false;
  }
  return !path.empty() && segment_matches(pattern.front(), path.front()) && full_match(path.subspan(1), pattern.subspan(1));
}

[[nodiscard]] bool full_match(fs::path const& path, std::string_view pattern)
{
  auto const segments = path | std::views::transform([](fs::path const& p) { return p.string(); }) | std::ranges::to<std::vector>();
  auto const pattern_segments =
      pattern | std::views::split('/') | std::views::transform([](auto&& s) { return std::string_view{s}; }) | std::ranges::to<std::vector>();
  return full_match(segments, pattern_segments);
}

}  // namespace

owners::owners(
    std::map<fs::path, std::optional<owner>> listed, std::map<fs::path, std::optional<claim>> base_directories,
    std::map<fs::path, std::optional<claim>> source_directories
)
    : listed_{std::move(listed)}, base_directories_{std::move(base_directories)}, source_directories_{std::move(source_directories)}
{
}

std::optional<owner> owners::of(fs::path const& path) const
{
  if (auto const found = listed_.find(path); found != listed_.end()) {
    return found->second;
  }
  for (auto const* index : {&base_directories_, &source_directories_}) {
    for (auto directory = path.parent_path();; directory = directory.parent_path()) {
      if (auto const found = index->find(directory); found != index->end()) {
        return found->second.transform([&](claim const& c) { return owner_of(c, path.lexically_relative(directory)); });
      }
      if (directory == directory.parent_path()) {
        break;
      }
    }
  }
  return std::nullopt;
}

std::set<std::string> find_external(model::project const& project)
{
  auto const build = nested_build_dir(project);
  std::set<std::string> result;
  for (auto const& target : project.targets()) {
    if (target.imported || !is_within(target.source_dir, project.source_dir()) || (build && is_within(target.source_dir, *build))) {
      result.insert(target.id);
    }
  }
  return result;
}

owners classify(model::project const& project)
{
  // External targets' files are an open decision (docs/design.md); custom
  // targets are not code; a header set verification target shares the source
  // directory of the target it verifies and compiles only generated files.
  auto const external = find_external(project);
  std::vector<model::target const*> owning;
  // The files of other external targets belong to no target, even inside a
  // directory a project target claims (a build directory in the source tree).
  // An imported target's source directory is only where it is declared.
  std::vector<model::target const*> shadowing;
  for (auto const& target : project.targets()) {
    if (!external.contains(target.id) && target.type != "UTILITY" && !target.verifies) {
      owning.push_back(&target);
    } else if (external.contains(target.id) && !target.imported) {
      shadowing.push_back(&target);
    }
  }

  std::vector<std::pair<fs::path, std::optional<owner>>> listed;
  std::vector<std::pair<fs::path, std::optional<claim>>> base_directories;
  std::vector<std::pair<fs::path, std::optional<claim>>> source_directories;
  for (auto const* target : owning) {
    claim const loose{target->id, is_public(*target, "")};
    for (auto const& path : target->sources) {
      listed.emplace_back(path, owner_of(loose, relative_to_deepest(path, std::span{&target->source_dir, 1})));
    }
    for (auto const& set : target->file_sets) {
      claim const c{target->id, is_public(*target, set.visibility)};
      for (auto const& path : set.files) {
        listed.emplace_back(path, owner_of(c, relative_to_deepest(path, set.base_directories)));
      }
      if (set.files.empty()) {
        for (auto const& directory : set.base_directories) {
          base_directories.emplace_back(directory, c);
        }
      }
    }
    source_directories.emplace_back(target->source_dir, loose);
  }
  for (auto const* target : shadowing) {
    for (auto const& path : target->sources) {
      listed.emplace_back(path, std::nullopt);
    }
    for (auto const& set : target->file_sets) {
      for (auto const& path : set.files) {
        listed.emplace_back(path, std::nullopt);
      }
      if (set.files.empty()) {
        for (auto const& directory : set.base_directories) {
          base_directories.emplace_back(directory, std::nullopt);
        }
      }
    }
    source_directories.emplace_back(target->source_dir, std::nullopt);
  }
  // Generated files, such as header set verification sources, belong only to a
  // target that lists them or a base directory inside the build directory. The
  // base directory tier is searched first, so shadowing the build directory
  // there also keeps it from a target whose source directory holds it.
  if (auto const build = nested_build_dir(project)) {
    base_directories.emplace_back(*build, std::nullopt);
  }
  return owners{indexed(listed), indexed(base_directories), indexed(source_directories)};
}

std::set<std::string> find_tests(model::project const& project, std::span<std::string_view const> directories, std::span<std::string_view const> names)
{
  auto const external = find_external(project);
  auto const matches = [&](model::target const& target) {
    if (external.contains(target.id) || target.verifies) {
      return false;
    }
    auto const directory = target.source_dir.lexically_relative(project.source_dir());
    auto const lists_file = (directory == "." ? fs::path{} : directory) / "CMakeLists.txt";
    return std::ranges::any_of(directories, [&](std::string_view d) { return full_match(lists_file, d); })
           || std::ranges::any_of(names, [&](std::string_view n) { return segment_matches(n, target.name); });
  };
  std::set<std::string> tests;
  for (auto const& target : project.targets()) {
    if (matches(target)) {
      tests.insert(target.id);
    }
  }
  for (auto const& target : project.targets()) {
    if (target.verifies && tests.contains(*target.verifies)) {
      tests.insert(target.id);
    }
  }
  return tests;
}

}  // namespace cpp_deps_checker::ownership
