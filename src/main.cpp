#include <iostream>

#include "cli/runner.hpp"

int main(int argc, char** argv) {
    return alignx::cli::run(argc, argv, std::cout, std::cerr);
}
