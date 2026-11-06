cmake_minimum_required(VERSION 4.2)
project(model_definitions LANGUAGES NONE)
# Traced into the same file, with its own add_library(second) first.
try_compile(scratch_ok PROJECT scratch
  SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/scratch NO_CACHE)
add_library(first INTERFACE)
ADD_LIBRARY(second INTERFACE)
function(make_library name)
  add_library(${name} INTERFACE)
endfunction()
make_library(third)
add_library(ext::x INTERFACE IMPORTED)
