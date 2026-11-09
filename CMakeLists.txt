cmake_minimum_required(VERSION 4.2)

project(cpp_deps_checker VERSION 0.1.0 LANGUAGES CXX)

include(CTest)

if(BUILD_TESTING)
    add_subdirectory(testing)
endif()

add_subdirectory(core)
