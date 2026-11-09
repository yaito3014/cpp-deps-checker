add_library(cpp_deps_checker_report)
add_library(cpp_deps_checker::report ALIAS cpp_deps_checker_report)
target_compile_features(cpp_deps_checker_report PUBLIC cxx_std_23)
target_sources(
    cpp_deps_checker_report
    PRIVATE src/report.cpp
    PUBLIC
        FILE_SET HEADERS
        BASE_DIRS include
        FILES include/cpp_deps_checker/report/report.hpp
)
target_link_libraries(
    cpp_deps_checker_report
    PUBLIC cpp_deps_checker::model cpp_deps_checker::rules
)

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
