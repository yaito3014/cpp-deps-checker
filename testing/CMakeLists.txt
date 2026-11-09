add_library(cpp_deps_checker_testing)
add_library(cpp_deps_checker::testing ALIAS cpp_deps_checker_testing)
target_compile_features(cpp_deps_checker_testing PUBLIC cxx_std_23)
target_sources(
    cpp_deps_checker_testing
    PRIVATE src/fixture.cpp
    PUBLIC
        FILE_SET HEADERS
        BASE_DIRS include
        FILES include/cpp_deps_checker/testing/fixture.hpp
)
target_compile_definitions(
    cpp_deps_checker_testing
    PRIVATE CPP_DEPS_CHECKER_SOURCE_DIR="${PROJECT_SOURCE_DIR}"
)
