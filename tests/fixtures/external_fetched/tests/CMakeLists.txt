add_executable(a_test a_test.cpp)
# Fetched after its consumer is defined, and consumed the way Catch2 is:
# the test includes dep's headers but links only dep_main.
include(FetchContent)
FetchContent_Declare(dep URL ${PROJECT_SOURCE_DIR}/fetched)
FetchContent_MakeAvailable(dep)
target_link_libraries(a_test PRIVATE a dep_main)
