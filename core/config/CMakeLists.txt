add_library(cpp_deps_checker_config)
add_library(cpp_deps_checker::config ALIAS cpp_deps_checker_config)
target_compile_features(cpp_deps_checker_config PUBLIC cxx_std_23)
# Linked into the clang-tidy plugin, a shared module.
set_target_properties(cpp_deps_checker_config PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_sources(
    cpp_deps_checker_config
    PRIVATE src/config.cpp
    PUBLIC
        FILE_SET HEADERS
        BASE_DIRS include
        FILES include/cpp_deps_checker/config/config.hpp
)
find_package(glaze CONFIG REQUIRED)
target_link_libraries(cpp_deps_checker_config PRIVATE glaze::glaze)

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
