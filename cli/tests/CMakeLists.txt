find_package(Catch2 3 CONFIG REQUIRED)
include(Catch)

add_executable(cpp_deps_checker_cli_test action_test.cpp cli_test.cpp expected_test.cpp)
target_link_libraries(
    cpp_deps_checker_cli_test
    PRIVATE
        cpp_deps_checker::cli
        cpp_deps_checker::ownership
        cpp_deps_checker::report
        cpp_deps_checker::testing
        Catch2::Catch2WithMain
)
target_compile_definitions(
    cpp_deps_checker_cli_test
    PRIVATE
        CPP_DEPS_CHECKER_EXECUTABLE="$<TARGET_FILE:cpp_deps_checker_main>"
        CPP_DEPS_CHECKER_SOURCE_DIR="${PROJECT_SOURCE_DIR}"
)
add_dependencies(cpp_deps_checker_cli_test cpp_deps_checker_main)
catch_discover_tests(cpp_deps_checker_cli_test)
