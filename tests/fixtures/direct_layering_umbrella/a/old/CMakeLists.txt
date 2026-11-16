add_subdirectory(d)
add_library(old INTERFACE)
target_include_directories(old INTERFACE include)
target_link_libraries(old INTERFACE d)
