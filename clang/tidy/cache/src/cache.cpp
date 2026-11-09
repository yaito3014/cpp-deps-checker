#include <cpp_deps_checker/tidy/cache.hpp>

#include <cpp_deps_checker/config/config.hpp>

#include <format>
#include <system_error>
#include <utility>

namespace cpp_deps_checker::tidy {

namespace {

namespace fs = std::filesystem;

// When the config file in source_dir was last written, if there is one.
[[nodiscard]] std::optional<fs::file_time_type> config_time(std::optional<fs::path> const& source_dir)
{
  if (!source_dir) {
    return std::nullopt;
  }
  std::error_code ec;
  auto const time = fs::last_write_time(*source_dir / config::file_name, ec);
  if (ec) {
    return std::nullopt;
  }
  return time;
}

}  // namespace

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
  if (auto const found = entries_.find(reply->build_dir);
      found != entries_.end() && found->second.index == reply->index && found->second.config_time == config_time(found->second.source_dir)) {
    return {found->second.context, std::nullopt};
  }
  auto& entry = entries_[reply->build_dir];
  entry = {reply->index, std::nullopt, std::nullopt, nullptr};
  auto project = model::load(reply->build_dir, std::nullopt);
  if (!project) {
    return {nullptr, std::format("cannot read the CMake File API reply in {}: {}", reply->build_dir.string(), project.error().message)};
  }
  entry.source_dir = project->source_dir();
  entry.config_time = config_time(entry.source_dir);
  auto const settings = config::load(*entry.source_dir);
  if (!settings) {
    return {nullptr, settings.error()};
  }
  auto ctx = rules::make_context(*std::move(project), *settings);
  if (!ctx) {
    return {nullptr, std::format("{}: {}", (*entry.source_dir / config::file_name).string(), ctx.error())};
  }
  entry.context = std::make_shared<rules::context const>(*std::move(ctx));
  return {entry.context, std::nullopt};
}

}  // namespace cpp_deps_checker::tidy
