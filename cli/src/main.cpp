#include <cpp_deps_checker/cli/cli.hpp>

#include <iostream>
#include <string>
#include <vector>

int main(int argc, char** argv)
{
  std::vector<std::string> const args(argv, argv + argc);
  return cpp_deps_checker::cli::run(args, std::cout, std::cerr);
}
