#include <cpp_deps_checker/rules/rules.hpp>

#include <algorithm>
#include <array>
#include <deque>
#include <expected>
#include <format>
#include <iterator>
#include <map>
#include <ranges>
#include <span>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace cpp_deps_checker::rules {

namespace {

namespace fs = std::filesystem;

[[nodiscard]] bool is_within(fs::path const& path, fs::path const& directory)
{
  auto const [rest, _] = std::ranges::mismatch(directory, path);
  return rest == directory.end();
}

[[nodiscard]] std::string located(model::project const& project, fs::path const& path) { return model::to_string(project.locate(path, std::nullopt)); }

[[nodiscard]] std::string joined(std::span<std::string const> parts, std::string_view separator)
{
  std::string result;
  for (auto const& [i, part] : std::views::enumerate(parts)) {
    result += (i == 0 ? "" : std::string{separator}) + part;
  }
  return result;
}

// A violation and the targets an exception names to allow it, as ids, in the
// order of the rule's keys: from and to, the target, or a cycle's members.
struct finding {
  violation found;
  std::vector<std::string> targets;
};

// Rule 1: no cycles.

// Tarjan's strongly connected components, each in model order. Iterative, so
// that a deep dependency chain cannot overflow the stack.
[[nodiscard]] std::vector<std::vector<std::string>> components(model::project const& project)
{
  std::unordered_map<std::string, std::size_t> position;
  for (auto const& [i, target] : std::views::enumerate(project.targets())) {
    position.try_emplace(target.id, static_cast<std::size_t>(i));
  }
  std::unordered_map<std::string, std::size_t> index;
  std::unordered_map<std::string, std::size_t> low;
  std::vector<std::string> stack;
  std::unordered_set<std::string> on_stack;
  std::vector<std::vector<std::string>> result;

  struct frame {
    std::string source;
    std::vector<model::dependency> dependencies;
    std::size_t next = 0;
  };
  auto const enter = [&](std::string const& source) {
    index[source] = low[source] = index.size();
    stack.push_back(source);
    on_stack.insert(source);
    auto const* target = project.find(source);
    return frame{source, target ? target->declared_dependencies() : std::vector<model::dependency>{}};
  };

  for (auto const& root : project.targets()) {
    if (index.contains(root.id)) {
      continue;
    }
    std::vector<frame> work;
    work.push_back(enter(root.id));
    while (!work.empty()) {
      auto& top = work.back();
      if (top.next < top.dependencies.size()) {
        auto const& target = top.dependencies[top.next++].target;
        if (!index.contains(target)) {
          work.push_back(enter(target));
        } else if (on_stack.contains(target)) {
          low[top.source] = std::min(low[top.source], index[target]);
        }
        continue;
      }
      auto const source = std::move(top.source);
      work.pop_back();
      if (!work.empty()) {
        auto const& parent = work.back().source;
        low[parent] = std::min(low[parent], low[source]);
      }
      if (low[source] == index[source]) {
        std::vector<std::string> members;
        while (members.empty() || members.back() != source) {
          members.push_back(stack.back());
          on_stack.erase(stack.back());
          stack.pop_back();
        }
        std::ranges::sort(members, {}, [&](std::string const& id) { return position.contains(id) ? position.at(id) : position.size(); });
        result.push_back(std::move(members));
      }
    }
  }
  return result;
}

using edge = std::pair<std::string, model::dependency>;  // source id, dependency

// Shortest path of edges from start back to start, within members.
[[nodiscard]] std::vector<edge> shortest_cycle(model::project const& project, std::string const& start, std::unordered_set<std::string> const& members)
{
  std::unordered_map<std::string, edge> parent;
  std::deque<std::string> queue{start};
  while (!queue.empty()) {
    auto const source = queue.front();
    queue.pop_front();
    for (auto const& dependency : project.find(source)->declared_dependencies()) {
      if (dependency.target == start) {
        std::vector<edge> path{{source, dependency}};
        while (path.back().first != start) {
          path.push_back(parent.at(path.back().first));
        }
        std::ranges::reverse(path);
        return path;
      }
      if (members.contains(dependency.target) && !parent.contains(dependency.target)) {
        parent.emplace(dependency.target, edge{source, dependency});
        queue.push_back(dependency.target);
      }
    }
  }
  return {};  // unreachable: start is in a strongly connected component
}

void cycles(context const& ctx, std::vector<finding>& out)
{
  // CMake rejects or drops a target linking itself, so a cycle always spans
  // more than one target.
  for (auto const& component : components(ctx.project)) {
    if (component.size() < 2) {
      continue;
    }
    auto const path = shortest_cycle(ctx.project, component.front(), {component.begin(), component.end()});
    std::vector<std::string> names;
    std::unordered_set<std::string> on_path;
    for (auto const& [source, _] : path) {
      names.push_back(ctx.project.find(source)->name);
      on_path.insert(source);
    }
    names.push_back(names.front());
    auto message = "dependency cycle: " + joined(names, " -> ");
    std::vector<std::string> others;
    for (auto const& id : component) {
      if (!on_path.contains(id)) {
        others.push_back(ctx.project.find(id)->name);
      }
    }
    if (!others.empty()) {
      message += " (also strongly connected: " + joined(others, ", ") + ")";
    }
    out.push_back({{"no-cycles", level::error, std::move(message), path.front().second.location}, component});
  }
}

// Rule 2: definition order.

void definition_order(context const& ctx, std::vector<finding>& out)
{
  for (auto const& consumer : ctx.project.targets()) {
    if (ctx.external.contains(consumer.id) || !consumer.definition) {
      continue;
    }
    for (auto const& dependency : consumer.declared_dependencies()) {
      auto const* target = ctx.project.find(dependency.target);
      if (!target || ctx.external.contains(target->id) || !target->definition) {
        continue;
      }
      if (target->definition->order > consumer.definition->order) {
        out.push_back({
            {
                "definition-order",
                level::error,
                std::format(
                    "{} links {}, which is defined later at {}; define {} before {}", consumer.name, target->name,
                    model::to_string(target->definition->location), target->name, consumer.name
                ),
                dependency.location,
            },
            {consumer.id, target->id},
        });
      }
    }
  }
}

// Rule 3: direct layering.

[[nodiscard]] bool depends_on(std::vector<model::dependency> const& field, std::string const& id)
{
  return std::ranges::contains(field, id, &model::dependency::target);
}

[[nodiscard]] std::optional<finding> direct_layering(context const& ctx, include const& directive)
{
  auto const includer = ctx.owners.of(directive.includer);
  auto const included = ctx.owners.of(directive.included);
  if (!includer || !included || includer->target == included->target) {
    return std::nullopt;
  }
  auto const& consumer = *ctx.project.find(includer->target);
  auto const& dependency = *ctx.project.find(included->target);
  auto const header = located(ctx.project, directive.included);
  std::string message;
  if (includer->is_public) {
    if (depends_on(consumer.interface_compile_dependencies, dependency.id)) {
      return std::nullopt;
    }
    auto const how = depends_on(consumer.compile_dependencies, dependency.id) ? "links PRIVATE" : "does not link";
    message = std::format("public header of {} includes {} of {}, which {} {}; link it PUBLIC", consumer.name, header, dependency.name, consumer.name, how);
  } else {
    if (depends_on(consumer.compile_dependencies, dependency.id)) {
      return std::nullopt;
    }
    message = std::format("{} includes {} of {} without linking {} directly", consumer.name, header, dependency.name, dependency.name);
  }
  return finding{
      {"direct-layering", level::error, std::move(message), ctx.project.locate(directive.includer, directive.line)},
      {consumer.id, dependency.id},
  };
}

// Rule 3a: self-contained headers.

void self_contained(context const& ctx, std::vector<finding>& out)
{
  for (auto const& failed : ctx.failures) {
    auto const* verified = ctx.project.verified_target(failed.source);
    if (!verified || ctx.external.contains(verified->id)) {
      continue;
    }
    if (!failed.error) {
      out.push_back({
          {
              "self-contained",
              level::error,
              std::format("header set verification of {} failed to preprocess {}", verified->name, failed.source.string()),
              std::nullopt,
          },
          {verified->id},
      });
    } else {
      out.push_back({
          {
              "self-contained",
              level::error,
              std::format("header of {} does not preprocess on its own: {}", verified->name, failed.error->message),
              ctx.project.locate(failed.error->file, failed.error->line),
          },
          {verified->id},
      });
    }
  }
}

// Rule 4: entry point only.

[[nodiscard]] std::optional<finding> entry_point(context const& ctx, include const& directive)
{
  auto const includer = ctx.owners.of(directive.includer);
  auto const included = ctx.owners.of(directive.included);
  if (!includer || !included || includer->target == included->target) {
    return std::nullopt;
  }
  auto const& consumer = ctx.project.find(includer->target)->name;
  auto const& owner = ctx.project.find(included->target)->name;
  auto const header = located(ctx.project, directive.included);
  std::string message;
  if (!included->is_public) {
    message = std::format("{} includes {}, a private header of {}; only {} may include it", consumer, header, owner, owner);
  } else if (included->detail && !ctx.tests.contains(includer->target)) {
    message = std::format("{} includes {}, a detail header of {}; only {} and test targets may include it", consumer, header, owner, owner);
  } else {
    return std::nullopt;
  }
  return finding{
      {"entry-point", level::error, std::move(message), ctx.project.locate(directive.includer, directive.line)},
      {includer->target, included->target},
  };
}

// Rule 5: nesting is privacy.

// The nearest proper ancestor of directory that defines a target. The
// top-level directory may be it, which constrains no consumer.
[[nodiscard]] std::optional<fs::path> private_to(fs::path const& directory, std::set<fs::path> const& defining)
{
  for (auto ancestor = directory.parent_path();; ancestor = ancestor.parent_path()) {
    if (defining.contains(ancestor)) {
      return ancestor;
    }
    if (ancestor == ancestor.parent_path()) {
      return std::nullopt;
    }
  }
}

[[nodiscard]] std::unordered_map<std::string, model::target const*> by_id(std::span<model::target const* const> targets)
{
  std::unordered_map<std::string, model::target const*> result;
  for (auto const* target : targets) {
    result.emplace(target->id, target);
  }
  return result;
}

void nesting(context const& ctx, std::vector<finding>& out)
{
  auto const production = ctx.production();
  auto const lookup = by_id(production);
  // A directory's targets share one subtree, so the parent is a directory:
  // this holds whether or not it defines several targets.
  std::set<fs::path> defining;
  for (auto const* target : production) {
    if (target->type != "UTILITY") {
      defining.insert(target->source_dir);
    }
  }
  for (auto const* consumer : production) {
    for (auto const& dependency : consumer->declared_dependencies()) {
      auto const found = lookup.find(dependency.target);
      if (found == lookup.end()) {
        continue;
      }
      auto const& target = *found->second;
      auto const parent = private_to(target.source_dir, defining);
      if (!parent || is_within(consumer->source_dir, *parent)) {
        continue;
      }
      out.push_back({
          {
              "nesting",
              level::error,
              std::format(
                  "{} links {}, which is private to {}/; hoist {}: a private detail is becoming shared API", consumer->name, target.name,
                  located(ctx.project, *parent), target.name
              ),
              dependency.location,
          },
          {consumer->id, target.id},
      });
    }
  }
}

// Rule 6: placement.

using node = std::optional<std::string>;  // a target id, or nullopt for the root above all roots

// Nodes reachable from the virtual root, in depth-first postorder.
[[nodiscard]] std::vector<node> postorder(std::span<std::string const> roots, std::map<std::string, std::vector<std::string>> const& successors)
{
  std::vector<node> order;
  std::set<node> seen{std::nullopt};
  struct frame {
    node at;
    std::span<std::string const> children;
    std::size_t next = 0;
  };
  std::vector<frame> stack{{std::nullopt, roots}};
  while (!stack.empty()) {
    auto& top = stack.back();
    if (top.next < top.children.size()) {
      auto const& child = top.children[top.next++];
      if (seen.insert(child).second) {
        stack.push_back({child, successors.at(child)});
      }
      continue;
    }
    order.push_back(top.at);
    stack.pop_back();
  }
  return order;
}

// Cooper, Harvey, and Kennedy, "A Simple, Fast Dominance Algorithm", with the
// nodes in the order they first got a dominator.
[[nodiscard]] std::vector<std::pair<node, node>> immediate_dominators(
    std::span<std::string const> roots, std::map<std::string, std::vector<std::string>> const& successors
)
{
  auto const order = postorder(roots, successors);
  std::map<node, std::size_t> number;
  std::map<node, std::vector<node>> predecessors;
  for (auto const& [i, n] : std::views::enumerate(order)) {
    number.emplace(n, static_cast<std::size_t>(i));
    predecessors.try_emplace(n);
  }
  for (auto const& root : roots) {
    predecessors[root].push_back(std::nullopt);
  }
  for (auto const& n : order) {
    if (n) {
      for (auto const& successor : successors.at(*n)) {
        predecessors[successor].push_back(n);
      }
    }
  }

  std::map<node, node> idom{{std::nullopt, std::nullopt}};
  std::vector<node> first_assigned{std::nullopt};
  auto const intersect = [&](node a, node b) {
    while (a != b) {
      while (number.at(a) < number.at(b)) {
        a = idom.at(a);
      }
      while (number.at(b) < number.at(a)) {
        b = idom.at(b);
      }
    }
    return a;
  };
  for (bool changed = true; changed;) {
    changed = false;
    for (auto const& n : order | std::views::take(order.size() - 1) | std::views::reverse) {  // the virtual root comes last
      std::vector<node> processed;
      for (auto const& p : predecessors.at(n)) {
        if (idom.contains(p)) {
          processed.push_back(p);
        }
      }
      auto updated = processed.front();
      for (auto const& p : processed | std::views::drop(1)) {
        updated = intersect(p, updated);
      }
      auto const found = idom.find(n);
      if (found == idom.end()) {
        idom.emplace(n, updated);
        first_assigned.push_back(n);
        changed = true;
      } else if (found->second != updated) {
        found->second = updated;
        changed = true;
      }
    }
  }
  std::vector<std::pair<node, node>> result;
  for (auto const& n : first_assigned) {
    result.emplace_back(n, idom.at(n));
  }
  return result;
}

void placement(context const& ctx, std::vector<finding>& out)
{
  auto const production = ctx.production();
  auto const lookup = by_id(production);
  std::map<std::string, std::vector<std::string>> successors;
  std::set<std::string> consumed;
  for (auto const* target : production) {
    auto& next = successors[target->id];
    for (auto const& dependency : target->declared_dependencies()) {
      if (lookup.contains(dependency.target)) {
        next.push_back(dependency.target);
        consumed.insert(dependency.target);
      }
    }
  }
  std::vector<std::string> roots;
  if (ctx.composition_roots) {
    roots = *ctx.composition_roots;
  } else {
    for (auto const* target : production) {
      if (!consumed.contains(target->id)) {
        roots.push_back(target->id);
      }
    }
  }
  for (auto const& [n, dominator] : immediate_dominators(roots, successors)) {
    if (!n || !dominator) {
      continue;
    }
    auto const& target = *lookup.at(*n);
    auto const& owner = *lookup.at(*dominator);
    if (is_within(target.source_dir, owner.source_dir)) {
      continue;
    }
    out.push_back({
        {
            "placement",
            level::warning,
            std::format("every path to {} goes through {}; move {} under {}/", target.name, owner.name, target.name, located(ctx.project, owner.source_dir)),
            target.definition.transform([](model::definition const& d) { return d.location; }),
        },
        {target.id},
    });
  }
}

// Rule 7: test isolation.

void test_isolation(context const& ctx, std::vector<finding>& out)
{
  for (auto const& consumer : ctx.project.targets()) {
    if (ctx.tests.contains(consumer.id)) {
      continue;
    }
    for (auto const& dependency : consumer.declared_dependencies()) {
      if (!ctx.tests.contains(dependency.target)) {
        continue;
      }
      out.push_back({
          {
              "test-isolation",
              level::error,
              std::format("{} links test target {}; only test targets may link it", consumer.name, ctx.project.find(dependency.target)->name),
              dependency.location,
          },
          {consumer.id, dependency.target},
      });
    }
  }
}

// Exceptions.

enum class keys { from_to, target, targets };

struct rule_keys {
  std::string_view rule;
  keys names;
  bool reads_includes;
};

constexpr std::array known_rules{
    rule_keys{"no-cycles", keys::targets, false},    rule_keys{"definition-order", keys::from_to, false}, rule_keys{"direct-layering", keys::from_to, true},
    rule_keys{"self-contained", keys::target, true}, rule_keys{"entry-point", keys::from_to, true},       rule_keys{"nesting", keys::from_to, false},
    rule_keys{"placement", keys::target, false},     rule_keys{"test-isolation", keys::from_to, false},
};

// Only for a rule of a resolved exception, which is known.
[[nodiscard]] rule_keys const& keys_of(std::string_view rule) { return *std::ranges::find(known_rules, rule, &rule_keys::rule); }

// entry with its target names resolved to ids.
[[nodiscard]] std::expected<exception, std::string> resolve(model::project const& project, config::exception const& entry)
{
  if (entry.rule.empty()) {
    return std::unexpected{std::string{"needs a rule"}};
  }
  auto const known = std::ranges::find(known_rules, entry.rule, &rule_keys::rule);
  if (known == known_rules.end()) {
    return std::unexpected{std::format("unknown rule {}", entry.rule)};
  }
  if (entry.reason.empty()) {
    return std::unexpected{std::string{"needs a reason"}};
  }
  std::vector<std::string> names;
  switch (known->names) {
    case keys::from_to:
      if (!entry.from || !entry.to || entry.target || entry.targets) {
        return std::unexpected{std::format("{} takes from and to", entry.rule)};
      }
      names = {*entry.from, *entry.to};
      break;
    case keys::target:
      if (!entry.target || entry.from || entry.to || entry.targets) {
        return std::unexpected{std::format("{} takes target", entry.rule)};
      }
      names = {*entry.target};
      break;
    case keys::targets:
      if (!entry.targets || entry.targets->empty() || entry.from || entry.to || entry.target) {
        return std::unexpected{std::format("{} takes targets", entry.rule)};
      }
      names = *entry.targets;
      break;
  }
  exception result{entry.rule, {}};
  for (auto const& name : names) {
    auto const found = std::ranges::find(project.targets(), name, &model::target::name);
    if (found == project.targets().end()) {
      return std::unexpected{std::format("no target named {}", name)};
    }
    result.targets.push_back(found->id);
  }
  return result;
}

[[nodiscard]] bool allows(exception const& allowed, finding const& f)
{
  if (allowed.rule != f.found.rule) {
    return false;
  }
  if (keys_of(allowed.rule).names == keys::targets) {
    return std::set<std::string>{allowed.targets.begin(), allowed.targets.end()} == std::set<std::string>{f.targets.begin(), f.targets.end()};
  }
  return allowed.targets == f.targets;
}

[[nodiscard]] std::string described(model::project const& project, exception const& allowed)
{
  auto const names = allowed.targets | std::views::transform([&](std::string const& id) { return project.find(id)->name; }) | std::ranges::to<std::vector>();
  if (keys_of(allowed.rule).names == keys::from_to) {
    return std::format("exception to {} from {} to {}", allowed.rule, names[0], names[1]);
  }
  return std::format("exception to {} for {}", allowed.rule, joined(names, ", "));
}

// The violations of found that no exception allows. With report_stale, also a
// warning for each exception that allows none, if its rule ran.
[[nodiscard]] std::vector<violation> excepted(context const& ctx, std::vector<finding> found, bool report_stale)
{
  std::vector<bool> used(ctx.exceptions.size());
  std::vector<violation> out;
  for (auto& f : found) {
    auto allowed = false;
    for (auto const& [i, e] : std::views::enumerate(ctx.exceptions)) {
      if (allows(e, f)) {
        used[static_cast<std::size_t>(i)] = true;
        allowed = true;
      }
    }
    if (!allowed) {
      out.push_back(std::move(f.found));
    }
  }
  if (!report_stale) {
    return out;
  }
  for (auto const& [i, e] : std::views::enumerate(ctx.exceptions)) {
    if (used[static_cast<std::size_t>(i)] || (!ctx.includes && keys_of(e.rule).reads_includes)) {
      continue;
    }
    out.push_back({
        "stale-exception",
        level::warning,
        described(ctx.project, e) + " matches no violation",
        model::location{std::string{config::file_name}, std::nullopt},
    });
  }
  return out;
}

}  // namespace

std::string_view to_string(level value) noexcept { return value == level::error ? "error" : "warning"; }

std::vector<model::target const*> context::production() const
{
  std::vector<model::target const*> result;
  for (auto const& target : project.targets()) {
    if (!external.contains(target.id) && !tests.contains(target.id) && !target.verifies) {
      result.push_back(&target);
    }
  }
  return result;
}

context make_context(model::project project)
{
  // An empty config names no composition root, so it cannot fail.
  return *make_context(std::move(project), config::config{});
}

std::expected<context, std::string> make_context(model::project project, config::config const& settings)
{
  auto const& tests_config = settings.tests.value_or(config::tests_config{});
  std::vector<std::string_view> directories{std::begin(ownership::default_test_directories), std::end(ownership::default_test_directories)};
  if (tests_config.directories) {
    directories.assign(tests_config.directories->begin(), tests_config.directories->end());
  }
  std::vector<std::string_view> names;
  if (tests_config.names) {
    names.assign(tests_config.names->begin(), tests_config.names->end());
  }
  auto owners = ownership::classify(project);
  auto tests = ownership::find_tests(project, directories, names);
  auto external = ownership::find_external(project);
  context ctx{std::move(project), {}, {}, std::move(owners), std::move(tests), std::move(external), std::nullopt, {}};
  if (settings.composition_roots) {
    auto const production = ctx.production();
    auto& roots = ctx.composition_roots.emplace();
    for (auto const& name : *settings.composition_roots) {
      auto const found = std::ranges::find(production, name, &model::target::name);
      if (found == production.end()) {
        return std::unexpected{std::format("composition_roots: no production target named {}", name)};
      }
      roots.push_back((*found)->id);
    }
  }
  for (auto const& [i, entry] : std::views::enumerate(settings.exceptions.value_or(std::vector<config::exception>{}))) {
    auto resolved = resolve(ctx.project, entry);
    if (!resolved) {
      return std::unexpected{std::format("exceptions[{}]: {}", i, resolved.error())};
    }
    ctx.exceptions.push_back(*std::move(resolved));
  }
  return ctx;
}

std::vector<violation> check(context const& ctx)
{
  std::vector<finding> out;
  cycles(ctx, out);
  definition_order(ctx, out);
  auto const includes = ctx.includes ? std::span<include const>{*ctx.includes} : std::span<include const>{};
  for (auto const& directive : includes) {
    if (auto v = direct_layering(ctx, directive)) {
      out.push_back(*std::move(v));
    }
  }
  self_contained(ctx, out);
  for (auto const& directive : includes) {
    if (auto v = entry_point(ctx, directive)) {
      out.push_back(*std::move(v));
    }
  }
  nesting(ctx, out);
  placement(ctx, out);
  test_isolation(ctx, out);
  return excepted(ctx, std::move(out), true);
}

std::vector<violation> check_include(context const& ctx, include const& directive)
{
  std::vector<finding> out;
  for (auto v : {direct_layering(ctx, directive), entry_point(ctx, directive)}) {
    if (v) {
      out.push_back(*std::move(v));
    }
  }
  return excepted(ctx, std::move(out), false);
}

}  // namespace cpp_deps_checker::rules
