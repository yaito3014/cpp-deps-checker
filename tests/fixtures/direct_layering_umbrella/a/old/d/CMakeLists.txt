add_library(d INTERFACE)
target_sources(d INTERFACE FILE_SET HEADERS BASE_DIRS include FILES include/d/d.hpp)
