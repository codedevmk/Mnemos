#include "cli.hpp"

#include <iostream>

int main(int argc, char** argv) {
    return mnemos::tools::main_cli(argc, argv, std::cout, std::cerr);
}
