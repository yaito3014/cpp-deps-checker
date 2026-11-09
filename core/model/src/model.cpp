#include <cpp_deps_checker/model/model.hpp>

#include <glaze/glaze.hpp>

#include <algorithm>
#include <cctype>
#include <format>
#include <fstream>
#include <map>
#include <ranges>
#include <sstream>
#include <tuple>
#include <utility>

// The parts of the File API reply and the trace that the model reads, members
// named as the JSON keys in snake_case. glaze reflects on them, which needs
// types with linkage, hence a named namespace private to this file rather than
// an unnamed one.
namespace cpp_deps_checker::model::json {

struct object_ref {
  std::string json_file;
};

struct index_json {
  std::map<std::string, glz::raw_json> reply;
};

struct paths_json {
  std::string source;
  std::string build;
};

struct configuration_json {
  std::vector<object_ref> targets;
  std::vector<object_ref> abstract_targets;
};

struct codemodel_json {
  paths_json paths;
  std::vector<configuration_json> configurations;
};

struct node_json {
  std::size_t file = 0;
  std::optional<int> line;
};

struct backtrace_graph_json {
  std::vector<node_json> nodes;
  std::vector<std::string> files;
};

struct link_item_json {
  std::optional<std::string> id;
  std::optional<std::size_t> backtrace;
  std::optional<glz::raw_json> from_dependency;
};

struct file_set_json {
  std::string name;
  std::string type;
  std::string visibility;
  std::vector<std::string> base_directories;
};

struct source_json {
  std::string path;
  std::optional<std::size_t> file_set_index;
  std::optional<std::vector<std::size_t>> file_set_indexes;
};

struct target_paths_json {
  std::string source;
};

struct target_json {
  std::string id;
  std::string name;
  std::string type;
  bool imported = false;
  std::optional<std::size_t> backtrace;
  backtrace_graph_json backtrace_graph;
  target_paths_json paths;
  std::vector<link_item_json> link_libraries;
  std::vector<link_item_json> compile_dependencies;
  std::vector<link_item_json> interface_link_libraries;
  std::vector<link_item_json> interface_compile_dependencies;
  std::vector<file_set_json> file_sets;
  std::vector<source_json> sources;
};

struct trace_entry_json {
  std::string cmd;
  std::string file;
  std::optional<int> line;
  std::vector<std::string> args;
};

}  // namespace cpp_deps_checker::model::json

template<>
struct glz::meta<cpp_deps_checker::model::json::object_ref> : glz::camel_case {};
template<>
struct glz::meta<cpp_deps_checker::model::json::configuration_json> : glz::camel_case {};
template<>
struct glz::meta<cpp_deps_checker::model::json::target_json> : glz::camel_case {};
template<>
struct glz::meta<cpp_deps_checker::model::json::link_item_json> : glz::camel_case {};
template<>
struct glz::meta<cpp_deps_checker::model::json::file_set_json> : glz::camel_case {};
template<>
struct glz::meta<cpp_deps_checker::model::json::source_json> : glz::camel_case {};

namespace cpp_deps_checker::model {

namespace {

namespace fs = std::filesystem;
using namespace json;

constexpr glz::opts json_options{.error_on_unknown_keys = false};

// file, line, target name
using site = std::tuple<fs::path, int, std::string>;

load_error error(load_error_kind kind, std::string message) { return {kind, std::move(message)}; }

template<class T>
std::expected<T, load_error> read_json(fs::path const& file)
{
  std::string buffer;
  T value{};
  if (auto const ec = glz::read_file_json<json_options>(value, file.string(), buffer)) {
    return std::unexpected{error(load_error_kind::malformed, std::format("cannot read {}: {}", file.string(), glz::format_error(ec, buffer)))};
  }
  return value;
}

[[nodiscard]] bool is_within(fs::path const& path, fs::path const& directory)
{
  auto const [rest, _] = std::ranges::mismatch(directory, path);
  return rest == directory.end();
}

std::expected<fs::path, load_error> current_index(fs::path const& reply_dir)
{
  // cmake-file-api(7): the current index has the largest name once the index-
  // or error- prefix is stripped. A failed configure writes an error index and
  // leaves the previous index-*.json in place.
  std::optional<std::pair<std::string, fs::path>> current;
  std::error_code ec;
  for (auto const& entry : fs::directory_iterator{reply_dir, ec}) {
    auto const name = entry.path().filename().string();
    if (!name.ends_with(".json") || !(name.starts_with("index-") || name.starts_with("error-"))) {
      continue;
    }
    auto key = name.substr(name.find('-') + 1);
    if (!current || key > current->first) {
      current.emplace(std::move(key), entry.path());
    }
  }
  if (!current) {
    return std::unexpected{error(load_error_kind::missing, std::format("no File API reply in {}; configure with a codemodel-v2 query", reply_dir.string()))};
  }
  if (current->second.filename().string().starts_with("error-")) {
    return std::unexpected{error(load_error_kind::stale, std::format("the last configure failed ({})", current->second.string()))};
  }
  return current->second;
}

std::optional<location> location_of(backtrace_graph_json const& graph, std::optional<std::size_t> backtrace)
{
  if (!backtrace || *backtrace >= graph.nodes.size()) {
    return std::nullopt;
  }
  // A target CMake creates itself (such as a header set verification target)
  // has only a file-level node, without a line.
  auto const& node = graph.nodes[*backtrace];
  return location{graph.files.at(node.file), node.line};
}

std::vector<dependency> dependencies(target_json const& data, std::vector<link_item_json> const& items)
{
  std::vector<dependency> result;
  for (auto const& item : items) {
    if (item.id && !item.from_dependency) {
      result.push_back({*item.id, location_of(data.backtrace_graph, item.backtrace)});
    }
  }
  return result;
}

bool contains_ignoring_case(std::string_view text, std::string_view word)
{
  return !std::ranges::search(text, word, {}, [](unsigned char c) { return std::tolower(c); }).empty();
}

std::expected<std::map<site, std::size_t>, load_error> trace_order(fs::path const& trace)
{
  // The trace also holds try_compile projects, so a call counts only through
  // the definition site the File API reports. Command names are
  // case-insensitive and the trace keeps their spelling.
  std::ifstream lines{trace};
  if (!lines) {
    return std::unexpected{error(load_error_kind::malformed, std::format("cannot read {}", trace.string()))};
  }
  std::map<site, std::size_t> order;
  std::string line;
  for (std::size_t position = 0; std::getline(lines, line); ++position) {
    // Most lines are other commands; this cheap test skips their parsing.
    if (!contains_ignoring_case(line, "add_library") && !contains_ignoring_case(line, "add_executable")) {
      continue;
    }
    trace_entry_json entry;
    if (auto const ec = glz::read<json_options>(entry, line)) {
      return std::unexpected{
          error(load_error_kind::malformed, std::format("cannot read {} line {}: {}", trace.string(), position + 1, glz::format_error(ec, line)))
      };
    }
    std::string command = entry.cmd;
    std::ranges::transform(command, command.begin(), [](unsigned char c) { return std::tolower(c); });
    if ((command == "add_library" || command == "add_executable") && entry.line && !entry.args.empty()) {
      order.try_emplace(site{fs::weakly_canonical(entry.file), *entry.line, entry.args.front()}, position);
    }
  }
  return order;
}

std::optional<definition> definition_of(target_json const& data, fs::path const& source_dir, std::map<site, std::size_t> const& order)
{
  auto const where = location_of(data.backtrace_graph, data.backtrace);
  if (!where || !where->line) {
    return std::nullopt;
  }
  auto const found = order.find(site{fs::weakly_canonical(source_dir / where->file), *where->line, data.name});
  if (found == order.end()) {
    return std::nullopt;
  }
  return definition{found->second, *where};
}

std::optional<std::string> verified_id(target_json const& data)
{
  // CMake names the target <target>_verify_interface_header_sets, links it to
  // <target> only, and gives it a backtrace without a line, since no command
  // in the project creates it; a user target of the same name has a line.
  auto const where = location_of(data.backtrace_graph, data.backtrace);
  if (!data.name.ends_with("_verify_interface_header_sets") || (where && where->line) || data.link_libraries.size() != 1) {
    return std::nullopt;
  }
  return data.link_libraries.front().id;
}

std::vector<std::size_t> file_set_indexes(source_json const& source)
{
  if (source.file_set_indexes) {
    return *source.file_set_indexes;
  }
  // Before codemodel 2.11 a source names only its most recent file set.
  if (source.file_set_index) {
    return {*source.file_set_index};
  }
  return {};
}

target to_target(target_json const& data, fs::path const& source_dir, std::map<site, std::size_t> const& order)
{
  auto const absolute = [&](std::string_view reported) { return fs::weakly_canonical(source_dir / reported); };

  std::vector<fs::path> loose;
  std::vector<std::vector<fs::path>> members(data.file_sets.size());
  for (auto const& source : data.sources) {
    auto const indexes = file_set_indexes(source);
    if (indexes.empty()) {
      loose.push_back(absolute(source.path));
    }
    for (auto const index : indexes) {
      members.at(index).push_back(absolute(source.path));
    }
  }
  std::vector<file_set> sets;
  for (auto const& [entry, files] : std::views::zip(data.file_sets, members)) {
    sets.push_back({
        .name = entry.name,
        .type = entry.type,
        .visibility = entry.visibility,
        .base_directories = entry.base_directories | std::views::transform(absolute) | std::ranges::to<std::vector>(),
        .files = files,
    });
  }

  return {
      .id = data.id,
      .name = data.name,
      .type = data.type,
      .imported = data.imported,
      .definition = definition_of(data, source_dir, order),
      .link_libraries = dependencies(data, data.link_libraries),
      .compile_dependencies = dependencies(data, data.compile_dependencies),
      .interface_link_libraries = dependencies(data, data.interface_link_libraries),
      .interface_compile_dependencies = dependencies(data, data.interface_compile_dependencies),
      .source_dir = absolute(data.paths.source),
      .sources = std::move(loose),
      .file_sets = std::move(sets),
      .verifies = verified_id(data),
  };
}

}  // namespace

std::string to_string(location const& where) { return where.line ? std::format("{}:{}", where.file.string(), *where.line) : where.file.string(); }

std::vector<dependency> target::declared_dependencies() const
{
  std::vector<dependency> first;
  for (auto const* field : {&link_libraries, &compile_dependencies, &interface_link_libraries, &interface_compile_dependencies}) {
    for (auto const& entry : *field) {
      if (!std::ranges::contains(first, entry.target, &dependency::target)) {
        first.push_back(entry);
      }
    }
  }
  return first;
}

project::project(std::vector<target> targets, std::filesystem::path source_dir, std::filesystem::path build_dir)
    : targets_{std::move(targets)}, source_dir_{std::move(source_dir)}, build_dir_{std::move(build_dir)}
{
  for (auto const [position, target] : std::views::enumerate(targets_)) {
    index_.try_emplace(target.id, static_cast<std::size_t>(position));
  }
}

target const* project::find(std::string_view id) const noexcept
{
  auto const found = index_.find(id);
  return found == index_.end() ? nullptr : &targets_[found->second];
}

location project::locate(std::filesystem::path const& path, std::optional<int> line) const
{
  return {is_within(path, source_dir_) ? path.lexically_relative(source_dir_) : path, line};
}

target const* project::verified_target(std::filesystem::path const& source) const noexcept
{
  for (auto const& target : targets_) {
    if (target.verifies && std::ranges::contains(target.sources, source)) {
      return find(*target.verifies);
    }
  }
  return nullptr;
}

std::expected<project, load_error> load(std::filesystem::path const& build_dir, std::optional<std::filesystem::path> const& trace)
{
  auto const reply_dir = build_dir / ".cmake" / "api" / "v1" / "reply";
  auto const index_file = current_index(reply_dir);
  if (!index_file) {
    return std::unexpected{index_file.error()};
  }
  auto const index = read_json<index_json>(*index_file);
  if (!index) {
    return std::unexpected{index.error()};
  }
  auto const codemodel_ref = index->reply.find("codemodel-v2");
  if (codemodel_ref == index->reply.end()) {
    return std::unexpected{
        error(load_error_kind::missing, std::format("no codemodel-v2 reply in {}; configure with a codemodel-v2 query", reply_dir.string()))
    };
  }
  object_ref ref;
  if (auto const ec = glz::read<json_options>(ref, codemodel_ref->second.str)) {
    return std::unexpected{error(load_error_kind::malformed, std::format("cannot read {}: {}", index_file->string(), glz::format_error(ec)))};
  }
  auto const codemodel = read_json<codemodel_json>(reply_dir / ref.json_file);
  if (!codemodel) {
    return std::unexpected{codemodel.error()};
  }
  // Which configuration a multi-config generator should be checked in is an
  // open decision in docs/design.md.
  if (codemodel->configurations.size() != 1) {
    return std::unexpected{error(
        load_error_kind::unsupported,
        std::format("the reply has {} configurations; multi-config generators are not supported yet", codemodel->configurations.size())
    )};
  }
  std::map<site, std::size_t> order;
  if (trace) {
    auto read = trace_order(*trace);
    if (!read) {
      return std::unexpected{read.error()};
    }
    order = *std::move(read);
  }

  auto const source_dir = fs::weakly_canonical(codemodel->paths.source);
  auto const& configuration = codemodel->configurations.front();
  std::vector<target> targets;
  for (auto const* refs : {&configuration.targets, &configuration.abstract_targets}) {
    for (auto const& entry : *refs) {
      auto const data = read_json<target_json>(reply_dir / entry.json_file);
      if (!data) {
        return std::unexpected{data.error()};
      }
      targets.push_back(to_target(*data, source_dir, order));
    }
  }
  return project{std::move(targets), source_dir, fs::weakly_canonical(codemodel->paths.build)};
}

}  // namespace cpp_deps_checker::model
