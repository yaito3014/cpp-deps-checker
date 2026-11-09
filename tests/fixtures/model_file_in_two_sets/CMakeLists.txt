cmake_minimum_required(VERSION 4.2)
project(model_file_in_two_sets LANGUAGES CXX)
add_library(x STATIC x.cpp)
# CMake accepts one file in several file sets of a target.
target_sources(x PUBLIC FILE_SET pub TYPE HEADERS BASE_DIRS include FILES include/x/x.hpp)
target_sources(x PRIVATE FILE_SET priv TYPE HEADERS BASE_DIRS include FILES include/x/x.hpp)
