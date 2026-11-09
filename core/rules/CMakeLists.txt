add_library(cpp_deps_checker_rules)
add_library(cpp_deps_checker::rules ALIAS cpp_deps_checker_rules)
target_compile_features(cpp_deps_checker_rules PUBLIC cxx_std_23)
# Linked into the clang-tidy plugin, a shared module.
set_target_properties(cpp_deps_checker_rules PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_sources(
    cpp_deps_checker_rules
    PRIVATE src/rules.cpp
    PUBLIC
        FILE_SET HEADERS
        BASE_DIRS include
        FILES include/cpp_deps_checker/rules/rules.hpp
)
target_link_libraries(
    cpp_deps_checker_rules
    PUBLIC cpp_deps_checker::model cpp_deps_checker::ownership
)

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
