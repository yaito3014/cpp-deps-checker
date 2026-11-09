cmake_minimum_required(VERSION 4.2)

project(cpp_deps_checker VERSION 0.1.0 LANGUAGES CXX)

include(CTest)

add_subdirectory(core)
add_subdirectory(clang)

if(BUILD_TESTING)
    add_subdirectory(testing)
endif()
