add_subdirectory(cache)

# Loaded by clang-tidy and clangd, which provide the clang-tidy framework.
add_library(cpp_deps_checker_tidy MODULE src/tidy.cpp)
set_target_properties(cpp_deps_checker_tidy PROPERTIES OUTPUT_NAME cpp-deps-checker-tidy)
target_compile_features(cpp_deps_checker_tidy PRIVATE cxx_std_23)
target_link_libraries(
    cpp_deps_checker_tidy
    PRIVATE
        cpp_deps_checker_tidy_cache
        cpp_deps_checker::includes
        cpp_deps_checker::rules
        cpp_deps_checker_llvm
)

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
