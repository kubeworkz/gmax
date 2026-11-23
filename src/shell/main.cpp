#include <iostream>
#include <string_view>

#include "core/version.h"

namespace {

// Exit codes are fixed by architecture 19.3; 1 is a usage error.
constexpr int kExitUsage = 1;

void usage() {
    std::cout << "usage: gmax [--version] [--help]" << std::endl;
    std::cout << std::endl;
    std::cout << "  --version  print the tool version and exit" << std::endl;
    std::cout << "  --help     print this message and exit" << std::endl;
}

}  // namespace

int main(int argc, char** argv) {
    for (int i = 1; i < argc; ++i) {
        const std::string_view arg(argv[i]);
        if (arg == "--version") {
            std::cout << "gmax " << gmax::version() << std::endl;
            return 0;
        }
        if (arg == "--help" || arg == "-h") {
            usage();
            return 0;
        }
        std::cerr << "gmax: unknown argument '" << arg << "'" << std::endl;
        usage();
        return kExitUsage;
    }
    usage();
    return kExitUsage;
}
