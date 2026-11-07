add_library(b INTERFACE)
target_sources(b INTERFACE FILE_SET HEADERS BASE_DIRS include FILES include/b/b.hpp)
target_link_libraries(b INTERFACE c)
