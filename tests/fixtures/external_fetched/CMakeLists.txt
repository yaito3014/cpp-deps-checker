cmake_minimum_required(VERSION 4.2)
project(external_fetched LANGUAGES CXX)
add_library(a INTERFACE)
add_subdirectory(tests)
