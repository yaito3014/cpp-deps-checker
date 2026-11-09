#ifndef CPP_DEPS_CHECKER_REPORT_REPORT_HPP
#define CPP_DEPS_CHECKER_REPORT_REPORT_HPP

#include <cpp_deps_checker/model/model.hpp>
#include <cpp_deps_checker/rules/rules.hpp>

#include <expected>
#include <filesystem>
#include <map>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace cpp_deps_checker::report {

// One compiler-style line: file:line: level: message [rule].
[[nodiscard]] std::string text(rules::violation const& violation);

// A GitHub Actions ::error or ::warning workflow command. GitHub attaches an
// annotation to a file by its path relative to the repository root, which root
// names; a location outside root goes into the message instead.
[[nodiscard]] std::string annotation(rules::violation const& violation, std::filesystem::path const& source_dir, std::filesystem::path const& root);

enum class kind { production, test, external };

using edge = std::pair<std::string, std::string>;  // consumer name, dependency name

struct graph {
  std::map<std::string, kind> nodes;  // by target name
  std::set<edge> edges;

  friend bool operator==(graph const&, graph const&) = default;
};

// The declared edges between targets, by name; tests and external are sets of
// target ids. Header set verification targets are left out because CMake
// creates them, and UTILITY targets because CMake does not let them be linked.
// Targets that share a name (imported targets in different directories) share
// a node.
[[nodiscard]] graph make_graph(model::project const& project, std::set<std::string> const& tests, std::set<std::string> const& external);

// A Markdown file holding the graph as a Mermaid flowchart, consumers above
// their dependencies, in a stable order.
[[nodiscard]] std::string mermaid(graph const& g);

// The graph in a file that mermaid wrote; an error message for anything else.
[[nodiscard]] std::expected<graph, std::string> parse_mermaid(std::string_view text);

// A Markdown job summary: every violation, since GitHub shows only a few
// annotations per step, then the edges the base branch's graph lacks (nullopt:
// no baseline graph). Paths are relative to root, as in annotation.
[[nodiscard]] std::string summary(
    std::span<rules::violation const> violations, std::filesystem::path const& source_dir, std::filesystem::path const& root,
    std::optional<std::vector<edge>> const& new_edges
);

}  // namespace cpp_deps_checker::report

#endif
