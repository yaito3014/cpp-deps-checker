add_subdirectory(a)
add_library(b STATIC b.cpp)
target_link_libraries(b PRIVATE a)
