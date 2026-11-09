#include <cpp_deps_checker/rules/rules.hpp>

#include <algorithm>
#include <deque>
#include <format>
#include <map>
#include <ranges>
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

void cycles(context const& ctx, std::vector<violation>& out)
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
    out.push_back({"no-cycles", level::error, std::move(message), path.front().second.location});
  }
}

// Rule 2: definition order.

void definition_order(context const& ctx, std::vector<violation>& out)
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
            "definition-order",
            level::error,
            std::format(
                "{} links {}, which is defined later at {}; define {} before {}", consumer.name, target->name, model::to_string(target->definition->location),
                target->name, consumer.name
            ),
            dependency.location,
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

[[nodiscard]] std::optional<violation> direct_layering(context const& ctx, include const& directive)
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
  return violation{"direct-layering", level::error, std::move(message), ctx.project.locate(directive.includer, directive.line)};
}

// Rule 3a: self-contained headers.

void self_contained(context const& ctx, std::vector<violation>& out)
{
  for (auto const& failed : ctx.failures) {
    auto const* verified = ctx.project.verified_target(failed.source);
    if (!verified || ctx.external.contains(verified->id)) {
      continue;
    }
    if (!failed.error) {
      out.push_back({
          "self-contained",
          level::error,
          std::format("header set verification of {} failed to preprocess {}", verified->name, failed.source.string()),
          std::nullopt,
      });
    } else {
      out.push_back({
          "self-contained",
          level::error,
          std::format("header of {} does not preprocess on its own: {}", verified->name, failed.error->message),
          ctx.project.locate(failed.error->file, failed.error->line),
      });
    }
  }
}

// Rule 4: entry point only.

[[nodiscard]] std::optional<violation> entry_point(context const& ctx, include const& directive)
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
  return violation{"entry-point", level::error, std::move(message), ctx.project.locate(directive.includer, directive.line)};
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

void nesting(context const& ctx, std::vector<violation>& out)
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
          "nesting",
          level::error,
          std::format(
              "{} links {}, which is private to {}/; hoist {}: a private detail is becoming shared API", consumer->name, target.name,
              located(ctx.project, *parent), target.name
          ),
          dependency.location,
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

void placement(context const& ctx, std::vector<violation>& out)
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
  // Until the consumer config names the composition roots, every production
  // target that no other production target links is one.
  std::vector<std::string> roots;
  for (auto const* target : production) {
    if (!consumed.contains(target->id)) {
      roots.push_back(target->id);
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
        "placement",
        level::warning,
        std::format("every path to {} goes through {}; move {} under {}/", target.name, owner.name, target.name, located(ctx.project, owner.source_dir)),
        target.definition.transform([](model::definition const& d) { return d.location; }),
    });
  }
}

// Rule 7: test isolation.

void test_isolation(context const& ctx, std::vector<violation>& out)
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
          "test-isolation",
          level::error,
          std::format("{} links test target {}; only test targets may link it", consumer.name, ctx.project.find(dependency.target)->name),
          dependency.location,
      });
    }
  }
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
  auto owners = ownership::classify(project);
  auto tests = ownership::find_tests(project);
  auto external = ownership::find_external(project);
  return {std::move(project), {}, {}, std::move(owners), std::move(tests), std::move(external)};
}

std::vector<violation> check(context const& ctx)
{
  std::vector<violation> out;
  cycles(ctx, out);
  definition_order(ctx, out);
  for (auto const& directive : ctx.includes) {
    if (auto v = direct_layering(ctx, directive)) {
      out.push_back(*std::move(v));
    }
  }
  self_contained(ctx, out);
  for (auto const& directive : ctx.includes) {
    if (auto v = entry_point(ctx, directive)) {
      out.push_back(*std::move(v));
    }
  }
  nesting(ctx, out);
  placement(ctx, out);
  test_isolation(ctx, out);
  return out;
}

std::vector<violation> check_include(context const& ctx, include const& directive)
{
  std::vector<violation> out;
  for (auto v : {direct_layering(ctx, directive), entry_point(ctx, directive)}) {
    if (v) {
      out.push_back(*std::move(v));
    }
  }
  return out;
}

}  // namespace cpp_deps_checker::rules
