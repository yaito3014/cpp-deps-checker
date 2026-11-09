add_subdirectory(c)
add_library(a STATIC a.cpp)
target_sources(a PUBLIC FILE_SET HEADERS BASE_DIRS include FILES include/a/a.hpp)
target_link_libraries(a PRIVATE c)
