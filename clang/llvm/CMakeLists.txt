# LLVM's exported targets carry no include directories or definitions.
add_library(cpp_deps_checker_llvm INTERFACE)
target_include_directories(cpp_deps_checker_llvm SYSTEM INTERFACE ${LLVM_INCLUDE_DIRS})
separate_arguments(CPP_DEPS_CHECKER_LLVM_DEFINITIONS NATIVE_COMMAND "${LLVM_DEFINITIONS}")
target_compile_definitions(cpp_deps_checker_llvm INTERFACE ${CPP_DEPS_CHECKER_LLVM_DEFINITIONS})
target_link_libraries(cpp_deps_checker_llvm INTERFACE "${CPP_DEPS_CHECKER_CLANG_CPP}" LLVM)
