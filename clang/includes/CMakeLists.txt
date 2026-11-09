add_library(cpp_deps_checker_includes)
add_library(cpp_deps_checker::includes ALIAS cpp_deps_checker_includes)
target_compile_features(cpp_deps_checker_includes PUBLIC cxx_std_23)
# Linked into the clang-tidy plugin, a shared module.
set_target_properties(cpp_deps_checker_includes PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_sources(
    cpp_deps_checker_includes
    PRIVATE src/includes.cpp
    PUBLIC
        FILE_SET HEADERS
        BASE_DIRS include
        FILES include/cpp_deps_checker/includes/includes.hpp
)
target_link_libraries(
    cpp_deps_checker_includes
    PUBLIC cpp_deps_checker::rules cpp_deps_checker_llvm
)
