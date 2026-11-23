// Deliberately framework-free: WP-001 may not fetch a dependency before the
// third_party freeze (TBD-11), and ctest only needs something honest to run.

#include <cstdlib>
#include <iostream>
#include <string_view>

#include "core/version.h"

namespace {

int failures = 0;

void expect(bool ok, std::string_view what) {
    if (!ok) {
        std::cerr << "FAIL " << what << std::endl;
        ++failures;
    }
}

bool looks_semver(std::string_view v) {
    int dots = 0;
    for (char c : v) {
        if (c == '.') {
            ++dots;
        } else if (c < '0' || c > '9') {
            return false;
        }
    }
    return dots == 2 && !v.empty();
}

}  // namespace

int main() {
    expect(!gmax::version().empty(), "version is not empty");
    expect(looks_semver(gmax::version()), "version is major.minor.patch");

    if (failures != 0) {
        std::cerr << failures << " expectation(s) failed" << std::endl;
        return EXIT_FAILURE;
    }
    std::cout << "version " << gmax::version() << std::endl;
    return EXIT_SUCCESS;
}
