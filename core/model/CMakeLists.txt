add_library(cpp_deps_checker_model)
add_library(cpp_deps_checker::model ALIAS cpp_deps_checker_model)
target_compile_features(cpp_deps_checker_model PUBLIC cxx_std_23)
# Linked into the clang-tidy plugin, a shared module.
set_target_properties(cpp_deps_checker_model PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_sources(
    cpp_deps_checker_model
    PRIVATE src/model.cpp
    PUBLIC
        FILE_SET HEADERS
        BASE_DIRS include
        FILES include/cpp_deps_checker/model/model.hpp
)
find_package(glaze CONFIG REQUIRED)
target_link_libraries(cpp_deps_checker_model PRIVATE glaze::glaze)

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
