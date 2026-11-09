#include <cpp_deps_checker/cli/cli.hpp>
#include <cpp_deps_checker/collect/collect.hpp>
#include <cpp_deps_checker/model/model.hpp>
#include <cpp_deps_checker/ownership/ownership.hpp>
#include <cpp_deps_checker/report/report.hpp>
#include <cpp_deps_checker/rules/rules.hpp>

#include <CLI/CLI.hpp>

#include <algorithm>
#include <expected>
#include <filesystem>
#include <format>
#include <fstream>
#include <iterator>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace cpp_deps_checker::cli {

namespace {

namespace fs = std::filesystem;

struct options {
  fs::path build_dir;
  std::optional<fs::path> trace;
  std::optional<fs::path> compile_commands;
  bool annotations = false;
  std::optional<fs::path> summary;
  std::optional<fs::path> baseline;
  std::optional<fs::path> output;

  [[nodiscard]] fs::path trace_or_default() const { return trace.value_or(build_dir / "trace.json"); }
};

[[nodiscard]] int unusable(std::ostream& err, std::string_view message)
{
  err << std::format("cpp-deps-checker: error: {}\n", message);
  return 2;
}

// Writes text to file in mode; false if the file could not be opened or
// written, or did not close cleanly.
[[nodiscard]] bool written(fs::path const& file, std::string_view text, std::ios::openmode mode)
{
  std::ofstream stream{file, mode};
  stream << text;
  stream.close();
  return !stream.fail();
}

[[nodiscard]] std::expected<std::string, std::string> read(fs::path const& file)
{
  std::ifstream in{file};
  if (!in) {
    return std::unexpected{std::format("cannot read {}", file.string())};
  }
  return std::string{std::istreambuf_iterator<char>{in}, {}};
}

// Every rule's input; without compile_commands there are no includes. A
// translation unit that fails to preprocess is a violation if it verifies a
// header set, and unusable input otherwise.
[[nodiscard]] std::expected<rules::context, std::string> context(
    fs::path const& build_dir, fs::path const& trace, std::optional<fs::path> const& compile_commands
)
{
  auto project = model::load(build_dir, trace);
  if (!project) {
    return std::unexpected{project.error().message};
  }
  auto ctx = rules::make_context(*std::move(project));
  if (!compile_commands) {
    return ctx;
  }
  // No rule looks at the includes of an external target's translation units,
  // and they may not preprocess before a build.
  std::set<fs::path> external_sources;
  for (auto const& target : ctx.project.targets()) {
    if (ctx.external.contains(target.id)) {
      external_sources.insert(target.sources.begin(), target.sources.end());
      for (auto const& set : target.file_sets) {
        external_sources.insert(set.files.begin(), set.files.end());
      }
    }
  }
  auto collection = collect::collect(
      *compile_commands, [&](fs::path const& path) { return ctx.owners.of(path).has_value(); },
      [&](fs::path const& source) { return external_sources.contains(source); }
  );
  if (!collection) {
    return std::unexpected{collection.error()};
  }
  for (auto const& failure : collection->failures) {
    if (!ctx.project.verified_target(failure.source)) {
      return std::unexpected{std::format("preprocessing {} failed:\n{}", failure.source.string(), failure.output)};
    }
  }
  ctx.includes = std::move(collection->includes);
  ctx.failures = std::move(collection->failures);
  return ctx;
}

[[nodiscard]] int check(options const& opts, std::ostream& out, std::ostream& err)
{
  auto compile_commands = opts.compile_commands;
  if (!compile_commands) {
    auto const fallback = opts.build_dir / "compile_commands.json";
    if (fs::exists(fallback)) {
      compile_commands = fallback;
    } else {
      err << std::format("cpp-deps-checker: warning: no {}; include rules skipped (configure with CMAKE_EXPORT_COMPILE_COMMANDS=ON)\n", fallback.string());
    }
  }
  std::optional<report::graph> baseline;
  if (opts.baseline) {
    auto const text = read(*opts.baseline);
    if (!text) {
      return unusable(err, text.error());
    }
    auto parsed = report::parse_mermaid(*text);
    if (!parsed) {
      return unusable(err, parsed.error());
    }
    baseline = *std::move(parsed);
  }
  auto const ctx = context(opts.build_dir, opts.trace_or_default(), compile_commands);
  if (!ctx) {
    return unusable(err, ctx.error());
  }

  auto const violations = rules::check(*ctx);
  auto const root = fs::weakly_canonical(fs::current_path());
  for (auto const& v : violations) {
    out << (opts.annotations ? report::annotation(v, ctx->project.source_dir(), root) : report::text(v)) << '\n';
  }
  if (opts.summary) {
    std::optional<std::vector<report::edge>> new_edges;
    if (baseline) {
      auto const edges = report::make_graph(ctx->project, ctx->tests, ctx->external).edges;
      new_edges.emplace();
      std::ranges::set_difference(edges, baseline->edges, std::back_inserter(*new_edges));
    }
    if (!written(*opts.summary, report::summary(violations, ctx->project.source_dir(), root, new_edges), std::ios::app)) {
      return unusable(err, std::format("cannot write {}", opts.summary->string()));
    }
  }
  return std::ranges::contains(violations, rules::level::error, &rules::violation::level) ? 1 : 0;
}

[[nodiscard]] int graph(options const& opts, std::ostream& out, std::ostream& err)
{
  auto const project = model::load(opts.build_dir, opts.trace_or_default());
  if (!project) {
    return unusable(err, project.error().message);
  }
  auto const text = report::mermaid(report::make_graph(*project, ownership::find_tests(*project), ownership::find_external(*project)));
  if (opts.output) {
    if (!written(*opts.output, text, std::ios::out)) {
      return unusable(err, std::format("cannot write {}", opts.output->string()));
    }
  } else {
    out << text;
  }
  return 0;
}

}  // namespace

int run(std::span<std::string const> args, std::ostream& out, std::ostream& err)
{
  CLI::App app{"Checks a CMake project's target graph and header includes against its layering rules", "cpp-deps-checker"};
  app.require_subcommand(1);
  options opts;
  auto* const check_command = app.add_subcommand("check", "check a configured CMake build directory");
  auto* const graph_command = app.add_subcommand("graph", "write the target graph as a Mermaid flowchart in Markdown");
  for (auto* const command : {check_command, graph_command}) {
    command->add_option("build_dir", opts.build_dir, "the CMake build directory")->required();
    command->add_option("--trace", opts.trace, "CMake JSON trace of the configure (default: BUILD_DIR/trace.json)");
  }
  check_command->add_option("--compile-commands", opts.compile_commands, "compilation database (default: BUILD_DIR/compile_commands.json)");
  check_command->add_flag(
      "--annotations", opts.annotations, "print GitHub Actions workflow commands instead of text, with paths relative to the current directory"
  );
  auto* const summary = check_command->add_option(
      "--summary", opts.summary,
      "append a Markdown summary to this file, with paths relative to the current directory (in GitHub Actions: $GITHUB_STEP_SUMMARY)"
  );
  check_command->add_option("--baseline", opts.baseline, "graph written by the graph command on the base branch; the summary lists the edges it lacks")
      ->needs(summary);
  graph_command->add_option("--output,-o", opts.output, "file to write (default: standard output)");

  std::vector<char const*> argv;
  for (auto const& arg : args) {
    argv.push_back(arg.c_str());
  }
  try {
    app.parse(static_cast<int>(argv.size()), argv.data());
  } catch (CLI::ParseError const& e) {
    // A usage error exits 2, as argparse does; help and version exit 0.
    return app.exit(e, out, err) == 0 ? 0 : 2;
  }
  return check_command->parsed() ? check(opts, out, err) : graph(opts, out, err);
}

}  // namespace cpp_deps_checker::cli
