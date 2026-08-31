// test_main.cpp -- Entry point for the unit test binary.
//
// usage: flightnet_tests [name-substring]
#include "test_framework.hpp"

#include <iostream>
#include <string>

int main(int argc, char** argv) {
    const std::string filter = (argc > 1) ? argv[1] : std::string();

    std::cout << "running " << ::testing::registry().size() << " tests";
    if (!filter.empty()) std::cout << " matching \"" << filter << "\"";
    std::cout << "\n\n";

    const int rc = ::testing::runAll(filter);
    std::cout << (rc == 0 ? "\nALL TESTS PASSED\n" : "\nTESTS FAILED\n");
    return rc;
}
