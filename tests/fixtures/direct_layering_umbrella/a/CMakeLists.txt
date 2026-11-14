add_subdirectory(umbrella)
add_subdirectory(old)
add_library(a STATIC a.cpp)
target_link_libraries(a PRIVATE umbrella old)
