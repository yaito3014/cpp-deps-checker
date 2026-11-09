#include <cpp_deps_checker/config/config.hpp>

#include <glaze/yaml.hpp>

#include <algorithm>
#include <format>
#include <fstream>
#include <ios>
#include <iterator>
#include <ranges>
#include <string_view>

namespace cpp_deps_checker::config {

namespace {

// glaze reports an error for a document without content, which YAML reads as
// null.
[[nodiscard]] bool has_content(std::string_view text)
{
  return std::ranges::any_of(text | std::views::split('\n'), [](auto const& line) {
    std::string_view const view{line};
    auto const start = view.find_first_not_of(" \t\r");
    return start != std::string_view::npos && view[start] != '#';
  });
}

}  // namespace

std::expected<config, std::string> load(std::filesystem::path const& source_dir)
{
  auto const file = source_dir / file_name;
  if (!std::filesystem::exists(file)) {
    return config{};
  }
  std::ifstream in{file};
  if (!in) {
    return std::unexpected{std::format("cannot read {}", file.string())};
  }
  // A directory opens, then fails to read, and libstdc++'s filebuf throws on a
  // read error.
  std::string text;
  try {
    text.assign(std::istreambuf_iterator<char>{in}, {});
  } catch (std::ios_base::failure const& error) {
    return std::unexpected{std::format("cannot read {}: {}", file.string(), error.what())};
  }
  config result;
  if (!has_content(text)) {
    return result;
  }
  if (auto const ec = glz::read_yaml(result, text)) {
    return std::unexpected{std::format("cannot read {}: {}", file.string(), glz::format_error(ec, text))};
  }
  return result;
}

}  // namespace cpp_deps_checker::config
