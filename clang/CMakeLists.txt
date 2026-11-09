find_package(LLVM 24 CONFIG REQUIRED)
# Clang's own CMake package also needs MLIR's on some distributions
# (apt.llvm.org), for targets this project does not use; the shared library
# and the headers, installed with LLVM's, are all it needs.
find_library(
    CPP_DEPS_CHECKER_CLANG_CPP
    NAMES clang-cpp
    HINTS "${LLVM_LIBRARY_DIR}"
    NO_DEFAULT_PATH
    REQUIRED
)

# LLVM's exported targets carry no include directories or definitions.
add_library(cpp_deps_checker_llvm INTERFACE)
target_include_directories(cpp_deps_checker_llvm SYSTEM INTERFACE ${LLVM_INCLUDE_DIRS})
separate_arguments(CPP_DEPS_CHECKER_LLVM_DEFINITIONS NATIVE_COMMAND "${LLVM_DEFINITIONS}")
target_compile_definitions(cpp_deps_checker_llvm INTERFACE ${CPP_DEPS_CHECKER_LLVM_DEFINITIONS})
target_link_libraries(cpp_deps_checker_llvm INTERFACE "${CPP_DEPS_CHECKER_CLANG_CPP}" LLVM)

add_subdirectory(includes)
add_subdirectory(collect)
add_subdirectory(tidy)
