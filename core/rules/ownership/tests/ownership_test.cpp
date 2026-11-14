#include <cpp_deps_checker/ownership/ownership.hpp>
#include <cpp_deps_checker/testing/factories.hpp>
#include <cpp_deps_checker/testing/fixture.hpp>

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <set>
#include <string>
#include <string_view>

namespace {

using namespace cpp_deps_checker;
using ownership::classify;
using ownership::find_external;
using ownership::find_tests;
using ownership::owner;
using testing::headers;
using testing::project;
using testing::root;
using testing::target;

std::optional<owner> owned(std::string id, bool is_public, bool detail = false) { return owner{std::move(id), is_public, detail}; }

}  // namespace

TEST_CASE("a listed source is private")
{
  auto const owners = classify(project({target("a", {.type = "STATIC_LIBRARY", .sources = {root / "a/a.cpp"}})}));
  CHECK(owners.of(root / "a/a.cpp") == owned("a", false));
}

TEST_CASE("file set visibility decides")
{
  auto const owners = classify(project({target(
      "a", {.type = "STATIC_LIBRARY",
            .file_sets = {
                headers("PUBLIC", {root / "a/include/a.hpp"}),
                headers("INTERFACE", {root / "a/include/i.hpp"}),
                headers("PRIVATE", {root / "a/src/p.hpp"}),
            }}
  )}));

  CHECK(owners.of(root / "a/include/a.hpp") == owned("a", true));
  CHECK(owners.of(root / "a/include/i.hpp") == owned("a", true));
  CHECK(owners.of(root / "a/src/p.hpp") == owned("a", false));
}

TEST_CASE("an unlisted file under the source directory is private")
{
  CHECK(classify(project({target("a", {.type = "STATIC_LIBRARY"})})).of(root / "a/x.hpp") == owned("a", false));
}

TEST_CASE("an interface library's files are public")
{
  auto const owners = classify(project({target("i", {.sources = {root / "i/listed.hpp"}})}));
  CHECK(owners.of(root / "i/listed.hpp") == owned("i", true));
  CHECK(owners.of(root / "i/unlisted.hpp") == owned("i", true));
}

TEST_CASE("the base directory of a file set without files")
{
  auto const c = target("c", {.source_dir = root / "src/c", .file_sets = {headers("INTERFACE", {}, {root / "include"})}});
  CHECK(classify(project({c})).of(root / "include/c.hpp") == owned("c", true));
}

TEST_CASE("the deepest source directory wins")
{
  auto const a = target("a", {.type = "STATIC_LIBRARY", .source_dir = root / "a"});
  auto const b = target("b", {.type = "STATIC_LIBRARY", .source_dir = root / "a/b"});
  CHECK(classify(project({a, b})).of(root / "a/b/x.hpp") == owned("b", false));
}

TEST_CASE("a listed file beats a directory")
{
  auto const a = target("a", {.type = "STATIC_LIBRARY", .sources = {root / "a/b/x.hpp"}});
  auto const b = target("b", {.type = "STATIC_LIBRARY", .source_dir = root / "a/b"});
  CHECK(classify(project({a, b})).of(root / "a/b/x.hpp") == owned("a", false));
}

TEST_CASE("a shared directory is ambiguous")
{
  auto const a = target("a", {.type = "STATIC_LIBRARY", .source_dir = root / "ab"});
  auto const b = target("b", {.type = "STATIC_LIBRARY", .source_dir = root / "ab"});
  CHECK(classify(project({a, b})).of(root / "ab/x.hpp") == std::nullopt);
}

TEST_CASE("a file listed by two targets is ambiguous")
{
  auto const a = target("a", {.type = "STATIC_LIBRARY", .sources = {root / "shared.hpp"}});
  auto const b = target("b", {.type = "STATIC_LIBRARY", .sources = {root / "shared.hpp"}});
  CHECK(classify(project({a, b})).of(root / "shared.hpp") == std::nullopt);
}

TEST_CASE("a file in a public and a private file set is public")
{
  auto const a = target("a", {.type = "STATIC_LIBRARY", .file_sets = {headers("PRIVATE", {root / "a/a.hpp"}), headers("PUBLIC", {root / "a/a.hpp"})}});
  CHECK(classify(project({a})).of(root / "a/a.hpp") == owned("a", true));
}

TEST_CASE("the base directory of a public and a private file set is public")
{
  auto const c =
      target("c", {.type = "STATIC_LIBRARY", .file_sets = {headers("PRIVATE", {}, {root / "c/include"}), headers("PUBLIC", {}, {root / "c/include"})}});
  CHECK(classify(project({c})).of(root / "c/include/c.hpp") == owned("c", true));
}

TEST_CASE("a build directory inside the source tree is claimed only by listing")
{
  auto const top = target("top", {.type = "STATIC_LIBRARY", .source_dir = root, .sources = {root / "build/generated.cpp"}});
  auto const owners = classify(project({top}, root / "build"));

  CHECK(owners.of(root / "build/lib/verify/a.hpp.cxx") == std::nullopt);
  CHECK(owners.of(root / "build/generated.cpp") == owned("top", false));
  CHECK(owners.of(root / "top.hpp") == owned("top", false));
}

TEST_CASE("a build directory inside a file set base directory is unclaimed")
{
  auto const a = target("a", {.source_dir = root, .file_sets = {headers("INTERFACE", {}, {root})}});
  auto const g = target("g", {.source_dir = root / "g", .file_sets = {headers("INTERFACE", {}, {root / "build/g/include"})}});
  auto const owners = classify(project({a, g}, root / "build"));

  CHECK(owners.of(root / "build/b/verify/b.hpp.cxx") == std::nullopt);
  CHECK(owners.of(root / "build/g/include/g.hpp") == owned("g", true));
  CHECK(owners.of(root / "a.hpp") == owned("a", true));
}

TEST_CASE("imported and utility targets own nothing")
{
  auto const ext = target("ext", {.imported = true, .sources = {root / "ext/e.hpp"}});
  auto const util = target("util", {.type = "UTILITY", .sources = {root / "util/u.txt"}});
  auto const owners = classify(project({ext, util}));

  CHECK(owners.of(root / "ext/e.hpp") == std::nullopt);
  CHECK(owners.of(root / "util/u.txt") == std::nullopt);
}

TEST_CASE("a file outside every target is unowned")
{
  CHECK(classify(project({target("a", {.type = "STATIC_LIBRARY"})})).of("/usr/include/vector") == std::nullopt);
}

TEST_CASE("a header set verification target owns nothing")
{
  auto const a = target("a", {.type = "STATIC_LIBRARY"});
  auto const verification =
      target("a_verify_interface_header_sets", {.type = "OBJECT_LIBRARY", .source_dir = root / "a", .sources = {root / "build/a/a.hpp.cxx"}, .verifies = "a"});
  auto const owners = classify(project({a, verification}));

  CHECK(owners.of(root / "a/x.hpp") == owned("a", false));
  CHECK(owners.of(root / "build/a/a.hpp.cxx") == std::nullopt);
}

TEST_CASE("detail below the file set base directory")
{
  auto const a = target(
      "a", {.type = "STATIC_LIBRARY",
            .source_dir = root / "detail/a",
            .file_sets = {
                headers("PUBLIC", {root / "detail/a/include/a/a.hpp", root / "detail/a/include/a/detail/impl.hpp"}, {root / "detail/a/include"}),
                headers("PRIVATE", {root / "detail/a/src/detail/p.hpp"}, {root / "detail/a/src"}),
            }}
  );
  auto const owners = classify(project({a}));

  // The detail/ above the base directory does not count.
  CHECK(owners.of(root / "detail/a/include/a/a.hpp") == owned("a", true));
  CHECK(owners.of(root / "detail/a/include/a/detail/impl.hpp") == owned("a", true, true));
  // Only public headers are detail headers; a private one stays private.
  CHECK(owners.of(root / "detail/a/src/detail/p.hpp") == owned("a", false));
}

TEST_CASE("detail below a base directory without files")
{
  auto const c = target("c", {.source_dir = root / "detail/c", .file_sets = {headers("INTERFACE", {}, {root / "detail/c/include"})}});
  auto const owners = classify(project({c}));

  CHECK(owners.of(root / "detail/c/include/c.hpp") == owned("c", true));
  CHECK(owners.of(root / "detail/c/include/detail/x.hpp") == owned("c", true, true));
}

TEST_CASE("detail below the source directory")
{
  auto const i = target("i", {.source_dir = root / "detail/i", .sources = {root / "detail/i/i.hpp", root / "detail/i/detail/listed.hpp"}});
  auto const owners = classify(project({i}));

  CHECK(owners.of(root / "detail/i/i.hpp") == owned("i", true));
  CHECK(owners.of(root / "detail/i/detail/listed.hpp") == owned("i", true, true));
  CHECK(owners.of(root / "detail/i/detail/x.hpp") == owned("i", true, true));
  // A file named detail is not in a detail/ directory.
  CHECK(owners.of(root / "detail/i/detail") == owned("i", true));
}

TEST_CASE("detail counts from the deepest base directory")
{
  auto const a =
      target("a", {.type = "STATIC_LIBRARY", .file_sets = {headers("PUBLIC", {root / "a/detail/include/a/a.hpp"}, {root / "a", root / "a/detail/include"})}});
  CHECK(classify(project({a})).of(root / "a/detail/include/a/a.hpp") == owned("a", true));
}

TEST_CASE("detail only if every public file set says so")
{
  auto const a = target(
      "a", {.type = "STATIC_LIBRARY",
            .file_sets = {
                headers("PUBLIC", {root / "a/include/detail/x.hpp"}, {root / "a/include"}),
                headers("PUBLIC", {root / "a/include/detail/x.hpp"}, {root / "a/include/detail"}),
                headers("PRIVATE", {root / "a/include/detail/y.hpp"}, {root / "a/include"}),
                headers("PUBLIC", {root / "a/include/detail/y.hpp"}, {root / "a/include"}),
            }}
  );
  auto const owners = classify(project({a}));

  CHECK(owners.of(root / "a/include/detail/x.hpp") == owned("a", true));
  CHECK(owners.of(root / "a/include/detail/y.hpp") == owned("a", true, true));
}

TEST_CASE("external targets")
{
  auto const p = project(
      {
          target("imported", {.imported = true}),
          target("outside", {.source_dir = "/elsewhere/dep"}),
          target("fetched", {.source_dir = root / "build/_deps/dep-src"}),
          target("project", {.source_dir = root / "src"}),
          target("top", {.source_dir = root}),
      },
      root / "build"
  );
  CHECK(find_external(p) == std::set<std::string>{"imported", "outside", "fetched"});
}

TEST_CASE("a build directory that is not strictly inside the source tree")
{
  auto const t = target("project", {.source_dir = root / "src"});
  CHECK(find_external(project({t}, root)).empty());
  CHECK(find_external(project({t}, root.parent_path())).empty());
  CHECK(find_external(project({t}, testing::default_build)).empty());
}

TEST_CASE("external targets own nothing and shadow enclosing owners")
{
  auto const fetched = root / "build/_deps/dep-src";
  auto const p = project(
      {
          target("top", {.source_dir = root}),
          target(
              "dep",
              {.type = "STATIC_LIBRARY", .source_dir = fetched, .sources = {fetched / "dep.cpp"}, .file_sets = {headers("PUBLIC", {}, {fetched / "include"})}}
          ),
      },
      root / "build"
  );
  auto const owners = classify(p);

  CHECK(owners.of(fetched / "dep.cpp") == std::nullopt);
  CHECK(owners.of(fetched / "include/dep/dep.hpp") == std::nullopt);
  CHECK(owners.of(fetched / "src/impl.hpp") == std::nullopt);
  CHECK(owners.of(root / "top.hpp") != std::nullopt);
}

TEST_CASE("a listed file of an external target is unowned")
{
  // Listed in a directory that a project target claims.
  auto const p = project({
      target("top", {.source_dir = root}),
      target("vendored", {.source_dir = "/elsewhere/vendored", .file_sets = {headers("INTERFACE", {root / "vendor/ext.hpp"})}}),
  });
  CHECK(classify(p).of(root / "vendor/ext.hpp") == std::nullopt);
}

TEST_CASE("an external base directory inside a project base directory")
{
  auto const vendor = root / "include/vendor";
  auto const p = project({
      target("lib", {.file_sets = {headers("PUBLIC", {}, {root / "include"})}}),
      target("vendored", {.source_dir = "/elsewhere/vendored", .file_sets = {headers("PUBLIC", {}, {vendor})}}),
  });
  auto const owners = classify(p);

  CHECK(owners.of(vendor / "v.hpp") == std::nullopt);
  CHECK(owners.of(root / "include/lib.hpp") != std::nullopt);
}

TEST_CASE("an imported target does not shadow the directory declaring it")
{
  // An imported target's source directory is where it is declared, not where
  // its files are.
  auto const p = project({target("top", {.source_dir = root}), target("ext", {.imported = true, .source_dir = root})});
  CHECK(classify(p).of(root / "top.hpp") != std::nullopt);
}

TEST_CASE("tests by defining directory")
{
  auto const p = project({
      target("top", {.source_dir = root / "tests"}),
      target("nested", {.source_dir = root / "tests/unit"}),
      target("singular", {.source_dir = root / "test"}),
      target("deeper", {.source_dir = root / "src/tests"}),
      target("deeper_singular", {.source_dir = root / "src/a/test/unit"}),
      target("lookalike", {.source_dir = root / "testsuite"}),
      target("deeper_lookalike", {.source_dir = root / "src/testsuite"}),
      target("production", {.source_dir = root / "src"}),
  });
  CHECK(find_tests(p) == std::set<std::string>{"top", "nested", "singular", "deeper", "deeper_singular"});
}

TEST_CASE("tests by name pattern")
{
  auto const p = project({target("a_test", {.source_dir = root / "src"}), target("a")});
  std::string_view const names[]{"*_test"};
  CHECK(find_tests(p, {}, names) == std::set<std::string>{"a_test"});
}

TEST_CASE("imported targets are never tests") { CHECK(find_tests(project({target("ext", {.imported = true, .source_dir = root / "tests"})})).empty()); }

TEST_CASE("external targets are never tests")
{
  auto const p = project(
      {target("fetched", {.source_dir = root / "build/_deps/dep-src"}), target("outside", {.source_dir = "/elsewhere/tests"}),
       target("a_test", {.source_dir = root / "tests"})},
      root / "build"
  );
  CHECK(find_tests(p) == std::set<std::string>{"a_test"});
}

TEST_CASE("a verification target follows the target it verifies")
{
  auto const p = project({
      target("a_test", {.source_dir = root / "src"}),
      target("a_test_verify_interface_header_sets", {.source_dir = root / "src", .verifies = "a_test"}),
      target("b"),
      target("b_verify_interface_header_sets", {.verifies = "b"}),
  });
  std::string_view const names[]{"*_test"};
  CHECK(find_tests(p, {}, names) == std::set<std::string>{"a_test", "a_test_verify_interface_header_sets"});
}

TEST_CASE("directory patterns match the whole CMakeLists.txt path, as full_match does")
{
  auto const p = project({
      target("deep", {.source_dir = root / "src/tests"}),
      target("unit", {.source_dir = root / "tests/unit"}),
      target("t1", {.source_dir = root / "t1"}),
      target("tx", {.source_dir = root / "tx"}),
  });
  std::string_view const any_depth[]{"*/tests/**"};
  std::string_view const one_level[]{"tests/*"};
  std::string_view const one_char[]{"t?/CMakeLists.txt"};
  std::string_view const digit[]{"t[0-9]/CMakeLists.txt"};
  std::string_view const not_digit[]{"t[!0-9]/CMakeLists.txt"};

  CHECK(find_tests(p, any_depth) == std::set<std::string>{"deep"});
  // * does not cross a directory: tests/unit/CMakeLists.txt is two levels.
  CHECK(find_tests(p, one_level).empty());
  CHECK(find_tests(p, one_char) == std::set<std::string>{"t1", "tx"});
  CHECK(find_tests(p, digit) == std::set<std::string>{"t1"});
  CHECK(find_tests(p, not_digit) == std::set<std::string>{"tx"});
}

TEST_CASE("a private file below detail/ is not a detail header")
{
  CHECK(classify(project({target("a", {.type = "STATIC_LIBRARY"})})).of(root / "a/detail/x.hpp") == owned("a", false));
}

TEST_CASE("a file set that lists files does not claim its base directory")
{
  auto const a = target("a", {.type = "STATIC_LIBRARY", .file_sets = {headers("PUBLIC", {root / "a/include/a/a.hpp"}, {root / "a/include"})}});
  // Not in the file set, so the source directory decides.
  CHECK(classify(project({a})).of(root / "a/include/a/other.hpp") == owned("a", false));
}

TEST_CASE("an external file set that lists files does not shadow its base directory")
{
  auto const p = project({
      target("lib", {.file_sets = {headers("PUBLIC", {}, {root / "include"})}}),
      target("vendored", {.source_dir = "/elsewhere/vendored", .file_sets = {headers("PUBLIC", {root / "include/v.hpp"}, {root / "include"})}}),
  });
  auto const owners = classify(p);

  CHECK(owners.of(root / "include/v.hpp") == std::nullopt);
  CHECK(owners.of(root / "include/lib.hpp") == owned("lib", true));
}

TEST_CASE("a target at the top level has CMakeLists.txt as its path")
{
  auto const p = project({target("top", {.source_dir = root})});
  std::string_view const top_level[]{"CMakeLists.txt"};
  CHECK(find_tests(p, top_level) == std::set<std::string>{"top"});
}

// Umbrella targets

namespace {

namespace fs = std::filesystem;

void touch(fs::path const& file)
{
  fs::create_directories(file.parent_path());
  std::ofstream{file};
}

std::set<std::string> umbrellas_of(std::vector<model::target> targets, fs::path const& top)
{
  auto const p = model::project{std::move(targets), top, top / "build"};
  return ownership::find_umbrellas(p, classify(p), find_external(p));
}

}  // namespace

TEST_CASE("an interface library that owns no header only links other targets")
{
  testing::temporary_directory const dir;
  auto const top = fs::canonical(dir.path());
  touch(top / "CMakeLists.txt");
  touch(top / "all.cpp");
  touch(top / "core/include/core/core.hpp");
  touch(top / "build/generated.hpp");
  touch(top / ".git/hooks/sample.h");

  auto const umbrellas = umbrellas_of(
      {
          target("all", {.source_dir = top, .links = {"core"}}),
          target("core", {.source_dir = top / "core"}),
      },
      top
  );

  CHECK(umbrellas == std::set<std::string>{"all"});
}

TEST_CASE("an umbrella is an interface library of the project without sources or file sets")
{
  testing::temporary_directory const dir;
  auto const top = fs::canonical(dir.path());

  auto const umbrellas = umbrellas_of(
      {
          target("all", {.source_dir = top / "all"}),
          target("static", {.type = "STATIC_LIBRARY", .source_dir = top / "static"}),
          target("listed", {.source_dir = top / "listed", .sources = {top / "listed/a.cpp"}}),
          target("abstract", {.source_dir = top / "abstract", .file_sets = {headers("INTERFACE", {}, {top / "abstract/include"})}}),
          target("imported", {.imported = true, .source_dir = top / "imported"}),
      },
      top
  );

  CHECK(umbrellas == std::set<std::string>{"all"});
}

TEST_CASE("a header no target owns keeps an interface library from being an umbrella")
{
  testing::temporary_directory const dir;
  auto const top = fs::canonical(dir.path());
  touch(top / "json/include/json/json.HPP");

  auto const umbrellas = umbrellas_of({target("json", {.source_dir = top / "json"}), target("json_all", {.source_dir = top / "json"})}, top);

  CHECK(umbrellas.empty());
}
