#include <cpp_deps_checker/collect/collect.hpp>
#include <cpp_deps_checker/testing/fixture.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <algorithm>
#include <format>
#include <fstream>
#include <ranges>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

namespace fs = std::filesystem;
using namespace cpp_deps_checker;
using rules::include;
using testing::temporary_directory;

void write(fs::path const& file, std::string_view text)
{
  fs::create_directories(file.parent_path());
  std::ofstream{file} << text;
}

std::string guarded(std::string_view name, std::string_view body = "")
{
  std::string guard{name};
  std::ranges::replace(guard, '/', '_');
  std::ranges::replace(guard, '.', '_');
  return std::format("#ifndef {0}\n#define {0}\n{1}#endif\n", guard, body);
}

// A compilation database in directory compiling each of sources with -Iinc
// and flags.
fs::path commands(
    fs::path const& directory, std::vector<std::string> const& sources, std::vector<std::string> const& flags = {}, std::string_view compiler = "clang++"
)
{
  std::string entries;
  for (auto const& source : sources) {
    auto arguments = std::format(R"("{}", "-Iinc")", compiler);
    for (auto const& flag : flags) {
      arguments += std::format(R"(, "{}")", flag);
    }
    entries += std::format(
        R"({}{{"directory": "{}", "arguments": [{}, "-c", "{}", "-o", "{}.o"], "file": "{}"}})", entries.empty() ? "" : ",", directory.string(), arguments,
        source, source, source
    );
  }
  auto const file = directory / "compile_commands.json";
  write(file, "[" + entries + "]");
  return file;
}

collect::collection collected(fs::path const& compile_commands, collect::predicate const& skip = {})
{
  auto result = collect::collect(compile_commands, [](fs::path const&) { return true; }, skip);
  if (!result) {
    throw std::runtime_error{result.error()};
  }
  return *std::move(result);
}

}  // namespace

TEST_CASE("every translation unit is run, header set verification included")
{
  temporary_directory const dir;
  auto const build = testing::configure(testing::fixture("direct_layering_public_private"), dir.path() / "build");
  auto const root = fs::canonical(testing::fixture("direct_layering_public_private"));
  auto const a_cpp = root / "a/a.cpp";
  auto const a_hpp = root / "a/include/a/a.hpp";
  auto const c_hpp = root / "a/c/include/c/c.hpp";
  auto const verify_a = fs::canonical(build.dir) / "a/a_verify_interface_header_sets/a/a.hpp.cxx";
  auto const verify_c = fs::canonical(build.dir) / "a/c/c_verify_interface_header_sets/c/c.hpp.cxx";

  auto const collection = collected(build.compile_commands);

  CHECK(
      collection.includes
      == std::vector<include>{
          {a_cpp, a_hpp, 1},
          {a_hpp, c_hpp, 3},
          {verify_a, a_hpp, 2},
          {verify_c, c_hpp, 2},
      }
  );
  // a links c PRIVATE, so a's verification lacks c's include directory.
  REQUIRE(collection.failures.size() == 1);
  CHECK(collection.failures.front().source == verify_a);
}

TEST_CASE("only the includers keep accepts are kept")
{
  temporary_directory const dir;
  auto const build = testing::configure(testing::fixture("direct_layering_public_private"), dir.path() / "build");

  auto const collection = collect::collect(build.compile_commands, [](fs::path const& p) { return p.extension() == ".cpp"; });

  REQUIRE(collection);
  REQUIRE(collection->includes.size() == 1);
  CHECK(collection->includes.front().includer.filename() == "a.cpp");
}

TEST_CASE("a translation unit that skip accepts is not run")
{
  temporary_directory const dir;
  write(dir.path() / "broken.cpp", "#include \"missing.hpp\"\n");
  auto const broken = fs::canonical(dir.path()) / "broken.cpp";

  auto const collection = collected(commands(dir.path(), {"broken.cpp"}), [&](fs::path const& p) { return p == broken; });

  CHECK(collection.includes.empty());
  CHECK(collection.failures.empty());
}

TEST_CASE("a preprocessing failure is returned with the includes so far")
{
  temporary_directory const dir;
  write(dir.path() / "ok.hpp", "");
  write(dir.path() / "m.cpp", "#include \"ok.hpp\"\n#include \"missing.hpp\"\n");
  auto const root = fs::canonical(dir.path());

  auto const collection = collected(commands(dir.path(), {"m.cpp"}));

  CHECK(collection.includes == std::vector<include>{{root / "m.cpp", root / "ok.hpp", 1}});
  REQUIRE(collection.failures.size() == 1);
  auto const& failure = collection.failures.front();
  CHECK(failure.source == root / "m.cpp");
  CHECK(failure.error == rules::diagnostic{root / "m.cpp", 2, "'missing.hpp' file not found"});
  CHECK_THAT(failure.output, Catch::Matchers::ContainsSubstring("missing.hpp"));
}

TEST_CASE("lines come from the directives the preprocessor ran")
{
  temporary_directory const dir;
  write(dir.path() / "inc/a.hpp", guarded("a.hpp"));
  write(dir.path() / "inc/b.hpp", guarded("b.hpp"));
  write(dir.path() / "inc/sub/c.hpp", guarded("c.hpp", "#include \"../b.hpp\"\n"));
  write(
      dir.path() / "m.cpp",
      "/*\n"
      "#include \"a.hpp\"\n"  // 2: commented out
      "*/\n"
      "#if 0\n"
      "#include \"a.hpp\"\n"  // 5: not compiled
      "#endif\n"
      "#include \\\n"  // 7: continued on line 8
      "  \"a.hpp\"\n"
      "#include \"sub/c.hpp\"\n"  // 9
      "#define HEADER \"b.hpp\"\n"
      "#include HEADER\n"  // 11: skipped by the guard
  );
  auto const root = fs::canonical(dir.path());
  auto const m = root / "m.cpp";
  auto const a = root / "inc/a.hpp";
  auto const b = root / "inc/b.hpp";
  auto const c = root / "inc/sub/c.hpp";

  auto const collection = collected(commands(dir.path(), {"m.cpp"}));

  std::vector<include> expected{{m, a, 7}, {m, c, 9}, {c, b, 3}, {m, b, 11}};
  std::ranges::sort(expected);
  CHECK(collection.includes == expected);
}

TEST_CASE("headers included with the include option")
{
  temporary_directory const dir;
  write(dir.path() / "inc/a.hpp", guarded("a.hpp", "#include \"b.hpp\"\n"));
  write(dir.path() / "inc/b.hpp", guarded("b.hpp"));
  write(dir.path() / "inc/c.hpp", guarded("c.hpp"));
  write(dir.path() / "m.cpp", "#include \"c.hpp\"\n");
  auto const root = fs::canonical(dir.path());

  auto const collection = collected(commands(dir.path(), {"m.cpp"}, {"-include", "a.hpp"}));

  // The -include header counts as included by the main file, without a line.
  std::vector<include> expected{
      {root / "m.cpp", root / "inc/a.hpp", std::nullopt}, {root / "inc/a.hpp", root / "inc/b.hpp", 3}, {root / "m.cpp", root / "inc/c.hpp", 1}
  };
  std::ranges::sort(expected);
  CHECK(collection.includes == expected);
}

TEST_CASE("lines are physical, whatever #line says")
{
  auto const renumbering = GENERATE(std::string{"#line 100\n"}, std::string{"# 50 \"m.cpp\"\n"}, std::string{"#line 7 \"other.cpp\"\n"});
  temporary_directory const dir;
  write(dir.path() / "inc/a.hpp", guarded("a.hpp", "#include \"b.hpp\"\n"));
  write(dir.path() / "inc/b.hpp", guarded("b.hpp"));
  write(dir.path() / "m.cpp", renumbering + "#include \"a.hpp\"\n");
  auto const root = fs::canonical(dir.path());

  auto const collection = collected(commands(dir.path(), {"m.cpp"}));

  std::vector<include> expected{{root / "m.cpp", root / "inc/a.hpp", 2}, {root / "inc/a.hpp", root / "inc/b.hpp", 3}};
  std::ranges::sort(expected);
  CHECK(collection.includes == expected);
}

TEST_CASE("a translation unit missing several headers keeps the lines it found")
{
  temporary_directory const dir;
  write(dir.path() / "inc/a.hpp", guarded("a.hpp", "#include \"b.hpp\"\n"));
  write(dir.path() / "inc/b.hpp", guarded("b.hpp"));
  write(dir.path() / "bad.cpp", "#include \"a.hpp\"\n#include \"missing1.hpp\"\n#include \"missing2.hpp\"\n");
  auto const root = fs::canonical(dir.path());

  auto const collection = collected(commands(dir.path(), {"bad.cpp"}));

  std::vector<include> expected{{root / "bad.cpp", root / "inc/a.hpp", 1}, {root / "inc/a.hpp", root / "inc/b.hpp", 3}};
  std::ranges::sort(expected);
  CHECK(collection.includes == expected);
  CHECK(collection.failures.size() == 1);
}

TEST_CASE("an unreadable compilation database is an error")
{
  temporary_directory const dir;
  CHECK_FALSE(collect::collect(dir.path() / "compile_commands.json", [](fs::path const&) { return true; }));
}

TEST_CASE("Clang's own headers are LLVM 24's, whatever the compile command's compiler")
{
  temporary_directory const dir;
  write(dir.path() / "m.cpp", "#include <stddef.h>\n");

  // A compiler that does not exist has no headers of its own to offer.
  auto const collection = collected(commands(dir.path(), {"m.cpp"}, {}, "/nonexistent/bin/clang++"));

  CHECK(collection.failures.empty());
  REQUIRE_FALSE(collection.includes.empty());
  CHECK_THAT(collection.includes.front().included.string(), Catch::Matchers::EndsWith("/clang/24/include/stddef.h"));
}

TEST_CASE("the first error is an error, not an earlier warning")
{
  temporary_directory const dir;
  write(dir.path() / "m.cpp", "#warning \"first\"\n#include \"missing.hpp\"\n");
  auto const root = fs::canonical(dir.path());

  auto const collection = collected(commands(dir.path(), {"m.cpp"}));

  REQUIRE(collection.failures.size() == 1);
  CHECK(collection.failures.front().error == rules::diagnostic{root / "m.cpp", 2, "'missing.hpp' file not found"});
}
