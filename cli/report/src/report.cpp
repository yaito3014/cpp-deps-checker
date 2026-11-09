#include <cpp_deps_checker/report/report.hpp>

#include <algorithm>
#include <format>
#include <ranges>
#include <regex>

namespace cpp_deps_checker::report {

namespace {

namespace fs = std::filesystem;

[[nodiscard]] bool is_within(fs::path const& path, fs::path const& directory)
{
  auto const [rest, _] = std::ranges::mismatch(directory, path);
  return rest == directory.end();
}

// location with its file relative to root if inside it, absolute if not.
[[nodiscard]] model::location from_root(model::location const& location, fs::path const& source_dir, fs::path const& root)
{
  auto const path = source_dir / location.file;
  return {is_within(path, root) ? path.lexically_relative(root) : path, location.line};
}

[[nodiscard]] std::string replaced(std::string_view text, std::span<std::pair<char, std::string_view> const> replacements)
{
  std::string result;
  for (char const c : text) {
    auto const found = std::ranges::find(replacements, c, &std::pair<char, std::string_view>::first);
    if (found == replacements.end()) {
      result += c;
    } else {
      result += found->second;
    }
  }
  return result;
}

// Workflow command escaping, as in @actions/core's command.ts.
[[nodiscard]] std::string escape_data(std::string_view value)
{
  static constexpr std::pair<char, std::string_view> replacements[]{{'%', "%25"}, {'\r', "%0D"}, {'\n', "%0A"}};
  return replaced(value, replacements);
}

[[nodiscard]] std::string escape_property(std::string_view value)
{
  static constexpr std::pair<char, std::string_view> replacements[]{{'%', "%25"}, {'\r', "%0D"}, {'\n', "%0A"}, {':', "%3A"}, {',', "%2C"}};
  return replaced(value, replacements);
}

constexpr std::string_view fence = "```";
constexpr std::string_view header[]{
    "```mermaid",
    "flowchart TD",
    "  classDef test stroke-dasharray: 4 4",
    "  classDef external font-style: italic",
};

// Mermaid entity codes, for the characters that would end the label or start
// an entity.
constexpr std::pair<char, std::string_view> escapes[]{{'#', "#35;"}, {'"', "#quot;"}};

[[nodiscard]] std::string unescaped(std::string_view label)
{
  std::string result;
  while (!label.empty()) {
    auto const found = std::ranges::find_if(escapes, [&](std::pair<char, std::string_view> const& escape) { return label.starts_with(escape.second); });
    if (found == std::ranges::end(escapes)) {
      result += label.front();
      label.remove_prefix(1);
    } else {
      result += found->first;
      label.remove_prefix(found->second.size());
    }
  }
  return result;
}

[[nodiscard]] std::string_view to_string(kind value) noexcept
{
  switch (value) {
    case kind::test:
      return "test";
    case kind::external:
      return "external";
    case kind::production:
      break;
  }
  return "production";
}

[[nodiscard]] std::string count(std::size_t n, std::string_view noun) { return std::format("{} {}{}", n, noun, n == 1 ? "" : "s"); }

[[nodiscard]] std::string cell(std::string_view value)
{
  static constexpr std::pair<char, std::string_view> replacements[]{{'|', "\\|"}, {'\n', " "}};
  return replaced(value, replacements);
}

}  // namespace

std::string text(rules::violation const& violation)
{
  auto const prefix = violation.location ? model::to_string(*violation.location) + ": " : "";
  return std::format("{}{}: {} [{}]", prefix, rules::to_string(violation.level), violation.message, violation.rule);
}

std::string annotation(rules::violation const& violation, std::filesystem::path const& source_dir, std::filesystem::path const& root)
{
  std::vector<std::pair<std::string_view, std::string>> properties;
  auto message = violation.message;
  if (violation.location) {
    auto const location = from_root(*violation.location, source_dir, root);
    if (location.file.is_absolute()) {
      message = model::to_string(location) + ": " + message;
    } else {
      properties.emplace_back("file", location.file.string());
      if (location.line) {
        properties.emplace_back("line", std::to_string(*location.line));
      }
    }
  }
  properties.emplace_back("title", violation.rule);
  std::string joined;
  for (auto const& [key, value] : properties) {
    joined += std::format("{}{}={}", joined.empty() ? "" : ",", key, escape_property(value));
  }
  return std::format("::{} {}::{}", rules::to_string(violation.level), joined, escape_data(message));
}

graph make_graph(model::project const& project, std::set<std::string> const& tests, std::set<std::string> const& external)
{
  auto const linkable = [](model::target const* t) { return t && !t->verifies && t->type != "UTILITY"; };
  std::set<std::string> linked_external;
  for (auto const& target : project.targets()) {
    if (linkable(&target) && !external.contains(target.id)) {
      for (auto const& dependency : target.declared_dependencies()) {
        if (external.contains(dependency.target)) {
          linked_external.insert(dependency.target);
        }
      }
    }
  }
  auto const shown = [&](model::target const* t) { return linkable(t) && (!external.contains(t->id) || linked_external.contains(t->id)); };
  graph result;
  for (auto const& target : project.targets()) {
    if (!shown(&target)) {
      continue;
    }
    auto const k = external.contains(target.id) ? kind::external : tests.contains(target.id) ? kind::test : kind::production;
    result.nodes.insert_or_assign(target.name, k);
    for (auto const& dependency : target.declared_dependencies()) {
      if (auto const* to = project.find(dependency.target); shown(to)) {
        result.edges.emplace(target.name, to->name);
      }
    }
  }
  return result;
}

std::string mermaid(graph const& g)
{
  std::map<std::string, std::string> ids;
  for (auto const& [i, name] : std::views::enumerate(g.nodes | std::views::keys)) {
    ids.emplace(name, std::format("n{}", i));
  }
  std::string result;
  for (auto const line : header) {
    result += std::format("{}\n", line);
  }
  for (auto const& [name, k] : g.nodes) {
    auto const suffix = k == kind::production ? std::string{} : std::format(":::{}", to_string(k));
    result += std::format("  {}[\"{}\"]{}\n", ids.at(name), replaced(name, escapes), suffix);
  }
  for (auto const& [from, to] : g.edges) {
    result += std::format("  {} --> {}\n", ids.at(from), ids.at(to));
  }
  return result + std::format("{}\n", fence);
}

std::expected<graph, std::string> parse_mermaid(std::string_view text)
{
  std::vector<std::string_view> lines;
  for (auto const line : text | std::views::split('\n')) {
    lines.emplace_back(line);
  }
  if (!lines.empty() && lines.back().empty() && text.ends_with('\n')) {
    lines.pop_back();
  }
  if (lines.size() < std::size(header) + 1 || !std::ranges::equal(lines | std::views::take(std::size(header)), header) || lines.back() != fence) {
    return std::unexpected{"not a Mermaid graph written by cpp-deps-checker"};
  }
  static std::regex const node_line{R"re(^  (n\d+)\["([^"]*)"\](?::::(test|external))?$)re"};
  static std::regex const edge_line{R"(^  (n\d+) --> (n\d+)$)"};
  std::map<std::string, std::string> names;
  graph result;
  for (std::size_t i = std::size(header); i + 1 < lines.size(); ++i) {
    std::string const line{lines[i]};
    std::smatch match;
    if (std::regex_match(line, match, node_line)) {
      auto name = unescaped(match.str(2));
      names.insert_or_assign(match.str(1), name);
      auto const k = match.str(3) == "test" ? kind::test : match.str(3) == "external" ? kind::external : kind::production;
      result.nodes.insert_or_assign(std::move(name), k);
    } else if (std::regex_match(line, match, edge_line) && names.contains(match.str(1)) && names.contains(match.str(2))) {
      result.edges.emplace(names.at(match.str(1)), names.at(match.str(2)));
    } else {
      return std::unexpected{std::format("line {} is not a node or an edge: {}", i + 1, line)};
    }
  }
  return result;
}

std::string summary(
    std::span<rules::violation const> violations, std::filesystem::path const& source_dir, std::filesystem::path const& root,
    std::optional<std::vector<edge>> const& new_edges
)
{
  std::string result = "## cpp-deps-checker\n\n";
  if (!violations.empty()) {
    auto const errors = static_cast<std::size_t>(std::ranges::count(violations, rules::level::error, &rules::violation::level));
    result += std::format("{}, {}.\n\n", count(errors, "error"), count(violations.size() - errors, "warning"));
    result += "| Location | Level | Rule | Message |\n| --- | --- | --- | --- |\n";
    for (auto const& v : violations) {
      auto const location = v.location ? std::format("`{}`", model::to_string(from_root(*v.location, source_dir, root))) : std::string{};
      result += std::format("| {} | {} | {} | {} |\n", cell(location), rules::to_string(v.level), v.rule, cell(v.message));
    }
  } else {
    result += "No violations.\n";
  }
  result += "\n### New edges\n\n";
  if (!new_edges) {
    result += "No baseline graph, so new edges are not listed.\n";
  } else if (new_edges->empty()) {
    result += "No new edges.\n";
  } else {
    for (auto const& [from, to] : *new_edges) {
      result += std::format("- `{}` -> `{}`\n", from, to);
    }
  }
  return result;
}

}  // namespace cpp_deps_checker::report
