find_package(Catch2 3 CONFIG REQUIRED)
find_package(glaze CONFIG REQUIRED)
include(Catch)

# The plugin runs only in the LLVM version it is built against.
find_program(
    CPP_DEPS_CHECKER_CLANG_TIDY
    clang-tidy
    HINTS "${LLVM_TOOLS_BINARY_DIR}"
    NO_DEFAULT_PATH
    REQUIRED
)
find_program(
    CPP_DEPS_CHECKER_CLANGD
    clangd
    HINTS "${LLVM_TOOLS_BINARY_DIR}"
    NO_DEFAULT_PATH
    REQUIRED
)

add_executable(cpp_deps_checker_tidy_test tidy_test.cpp)
target_link_libraries(
    cpp_deps_checker_tidy_test
    PRIVATE cpp_deps_checker::testing glaze::glaze Catch2::Catch2WithMain
)
target_compile_definitions(
    cpp_deps_checker_tidy_test
    PRIVATE
        CPP_DEPS_CHECKER_TIDY_PLUGIN="$<TARGET_FILE:cpp_deps_checker_tidy>"
        CPP_DEPS_CHECKER_CLANG_TIDY="${CPP_DEPS_CHECKER_CLANG_TIDY}"
        CPP_DEPS_CHECKER_CLANGD="${CPP_DEPS_CHECKER_CLANGD}"
)
add_dependencies(cpp_deps_checker_tidy_test cpp_deps_checker_tidy)
catch_discover_tests(cpp_deps_checker_tidy_test)
