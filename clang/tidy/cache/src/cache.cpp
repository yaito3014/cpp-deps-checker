#include <cpp_deps_checker/tidy/cache.hpp>

#include <format>
#include <utility>

namespace cpp_deps_checker::tidy {

lookup context_cache::find(std::filesystem::path const& working_dir)
{
  std::scoped_lock const lock{mutex_};
  auto const reply = model::find_reply(working_dir);
  if (!reply) {
    if (!without_reply_.insert(working_dir).second) {
      return {};
    }
    return {nullptr, std::format("no CMake File API reply in {} or above; configure with a codemodel-v2 query", working_dir.string())};
  }
  if (auto const found = entries_.find(reply->build_dir); found != entries_.end() && found->second.index == reply->index) {
    return {found->second.context, std::nullopt};
  }
  auto project = model::load(reply->build_dir, std::nullopt);
  auto& entry = entries_[reply->build_dir];
  entry.index = reply->index;
  if (!project) {
    entry.context = nullptr;
    return {nullptr, std::format("cannot read the CMake File API reply in {}: {}", reply->build_dir.string(), project.error().message)};
  }
  entry.context = std::make_shared<rules::context const>(rules::make_context(*std::move(project)));
  return {entry.context, std::nullopt};
}

}  // namespace cpp_deps_checker::tidy
