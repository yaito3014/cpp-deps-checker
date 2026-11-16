add_subdirectory(c)
add_library(umbrella INTERFACE)
target_link_libraries(umbrella INTERFACE c)
