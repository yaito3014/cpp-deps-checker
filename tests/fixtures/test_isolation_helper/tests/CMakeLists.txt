add_library(test_helper STATIC helper.cpp)
add_executable(helper_test helper_test.cpp)
target_link_libraries(helper_test PRIVATE test_helper)
