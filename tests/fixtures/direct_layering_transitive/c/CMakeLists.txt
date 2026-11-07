add_library(c INTERFACE)
target_sources(c INTERFACE FILE_SET HEADERS BASE_DIRS include FILES include/c/c.hpp)
