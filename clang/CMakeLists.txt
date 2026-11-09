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

# In a directory of its own: a target defined here would make every target
# below private to clang/ (rule 5), and the command line links collect.
add_subdirectory(llvm)
add_subdirectory(includes)
add_subdirectory(tidy)
