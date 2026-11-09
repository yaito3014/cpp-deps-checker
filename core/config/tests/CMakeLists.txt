find_package(Catch2 3 CONFIG REQUIRED)
include(Catch)

add_executable(cpp_deps_checker_config_test config_test.cpp)
target_link_libraries(
    cpp_deps_checker_config_test
    PRIVATE
        cpp_deps_checker::config
        cpp_deps_checker::testing
        Catch2::Catch2WithMain
)
catch_discover_tests(cpp_deps_checker_config_test)
