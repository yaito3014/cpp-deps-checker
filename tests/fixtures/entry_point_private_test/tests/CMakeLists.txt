add_executable(a_test a_test.cpp)
target_link_libraries(a_test PRIVATE a)
target_include_directories(a_test PRIVATE ${PROJECT_SOURCE_DIR}/a/src)
