add_library(cpp_deps_checker_ownership)
add_library(cpp_deps_checker::ownership ALIAS cpp_deps_checker_ownership)
target_compile_features(cpp_deps_checker_ownership PUBLIC cxx_std_23)
target_sources(
    cpp_deps_checker_ownership
    PRIVATE src/ownership.cpp
    PUBLIC
        FILE_SET HEADERS
        BASE_DIRS include
        FILES include/cpp_deps_checker/ownership/ownership.hpp
)
target_link_libraries(cpp_deps_checker_ownership PUBLIC cpp_deps_checker::model)

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
