#ifndef CPP_DEPS_CHECKER_CLI_CLI_HPP
#define CPP_DEPS_CHECKER_CLI_CLI_HPP

#include <ostream>
#include <span>
#include <string>

namespace cpp_deps_checker::cli {

// The command line on args, args[0] being the program name, writing to out and
// err. The exit status: 0 clean or warnings only, 1 errors found, 2 unusable
// input, an output file that could not be written, or a usage error.
[[nodiscard]] int run(std::span<std::string const> args, std::ostream& out, std::ostream& err);

}  // namespace cpp_deps_checker::cli

#endif
