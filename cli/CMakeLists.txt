add_subdirectory(report)

add_library(cpp_deps_checker_cli)
add_library(cpp_deps_checker::cli ALIAS cpp_deps_checker_cli)
target_compile_features(cpp_deps_checker_cli PUBLIC cxx_std_23)
target_sources(
    cpp_deps_checker_cli
    PRIVATE src/cli.cpp
    PUBLIC
        FILE_SET HEADERS
        BASE_DIRS include
        FILES include/cpp_deps_checker/cli/cli.hpp
)
find_package(CLI11 CONFIG REQUIRED)
target_link_libraries(
    cpp_deps_checker_cli
    PRIVATE
        cpp_deps_checker::collect
        cpp_deps_checker::model
        cpp_deps_checker::ownership
        cpp_deps_checker::report
        cpp_deps_checker::rules
        CLI11::CLI11
)

# The composition root.
add_executable(cpp_deps_checker_main src/main.cpp)
set_target_properties(cpp_deps_checker_main PROPERTIES OUTPUT_NAME cpp-deps-checker)
target_link_libraries(cpp_deps_checker_main PRIVATE cpp_deps_checker::cli)

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
