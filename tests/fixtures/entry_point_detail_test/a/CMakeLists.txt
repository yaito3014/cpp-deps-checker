add_library(a INTERFACE)
target_sources(a INTERFACE FILE_SET HEADERS BASE_DIRS include FILES include/a/a.hpp include/a/detail/impl.hpp)
