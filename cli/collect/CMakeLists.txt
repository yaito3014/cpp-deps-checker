add_library(cpp_deps_checker_collect)
add_library(cpp_deps_checker::collect ALIAS cpp_deps_checker_collect)
target_compile_features(cpp_deps_checker_collect PUBLIC cxx_std_23)
target_sources(
    cpp_deps_checker_collect
    PRIVATE src/collect.cpp
    PUBLIC
        FILE_SET HEADERS
        BASE_DIRS include
        FILES include/cpp_deps_checker/collect/collect.hpp
)
target_link_libraries(
    cpp_deps_checker_collect
    PUBLIC cpp_deps_checker::rules
    PRIVATE cpp_deps_checker::includes cpp_deps_checker_llvm
)

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
