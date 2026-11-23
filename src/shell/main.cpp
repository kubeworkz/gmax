#include <iostream>
#include <string>
#include <vector>

#include "core/version.h"
#include "shell/cli.h"

int main(int argc, char** argv) {
    std::vector<std::string> args;
    args.reserve(argc > 0 ? static_cast<std::size_t>(argc - 1) : 0);
    for (int i = 1; i < argc; ++i) {
        args.emplace_back(argv[i]);
    }

    gmax::shell::Cli cli(std::string(gmax::version()));
    return static_cast<int>(cli.run(args, std::cout, std::cerr));
}
