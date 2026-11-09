add_library(cpp_deps_checker_tidy_cache)
target_compile_features(cpp_deps_checker_tidy_cache PUBLIC cxx_std_23)
# Linked into the clang-tidy plugin, a shared module.
set_target_properties(cpp_deps_checker_tidy_cache PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_sources(
    cpp_deps_checker_tidy_cache
    PRIVATE src/cache.cpp
    PUBLIC
        FILE_SET HEADERS
        BASE_DIRS include
        FILES include/cpp_deps_checker/tidy/cache.hpp
)
target_link_libraries(
    cpp_deps_checker_tidy_cache
    PUBLIC cpp_deps_checker::rules
    PRIVATE cpp_deps_checker::model
)

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
