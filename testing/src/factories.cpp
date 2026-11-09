#include <cpp_deps_checker/testing/factories.hpp>

#include <utility>

namespace cpp_deps_checker::testing {

model::target target(std::string const& name, target_options options)
{
  std::vector<model::dependency> links;
  for (auto& id : options.links) {
    links.push_back({std::move(id), std::nullopt});
  }
  return {
      .id = name,
      .name = name,
      .type = std::move(options.type),
      .imported = options.imported,
      .definition = std::nullopt,
      .link_libraries = {},
      .compile_dependencies = {},
      .interface_link_libraries = std::move(links),
      .interface_compile_dependencies = {},
      .source_dir = options.source_dir.value_or(root / name),
      .sources = std::move(options.sources),
      .file_sets = std::move(options.file_sets),
      .verifies = std::move(options.verifies),
  };
}

model::target placed(std::string const& name, std::string_view directory, std::vector<std::string> links, target_options options)
{
  options.source_dir = directory == "." ? root : root / directory;
  options.links = std::move(links);
  return target(name, std::move(options));
}

model::file_set headers(std::string visibility, std::vector<std::filesystem::path> files, std::vector<std::filesystem::path> base_directories)
{
  return {
      .name = "HEADERS",
      .type = "HEADERS",
      .visibility = std::move(visibility),
      .base_directories = std::move(base_directories),
      .files = std::move(files),
  };
}

model::project project(std::vector<model::target> targets, std::filesystem::path build_dir)
{
  return model::project{std::move(targets), root, std::move(build_dir)};
}

}  // namespace cpp_deps_checker::testing
