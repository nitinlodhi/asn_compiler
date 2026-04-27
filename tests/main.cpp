#include "tests.h"
#include "utils/Logger.h"
#include <iostream>

int main() {
    std::cout << "Running ASN.1 Compiler Tests...\n" << std::endl;

    try {
        asn1::utils::TestFramework testRunner;

        register_lexer_tests(testRunner);
        register_parser_tests(testRunner);
        register_resolver_tests(testRunner);
        register_runtime_bitstream_tests(testRunner);
        register_runtime_codecs_tests(testRunner);
        register_runtime_utils_tests(testRunner);

        testRunner.printResults();
        return testRunner.allTestsPassed() ? 0 : 1;
    } catch (const std::exception& e) {
        asn1::utils::Logger::error("An unexpected error occurred: " + std::string(e.what()));
        return 1;
    }
}