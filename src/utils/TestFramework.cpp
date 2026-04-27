#include "utils/TestFramework.h"
#include <iostream>

namespace asn1::utils {

TestFramework::TestFramework() : totalTests(0), passedTests(0) {}

void TestFramework::addTest(const std::string& testName, std::function<bool()> testFunc) {
    totalTests++;
    TestResult result;
    result.testName = testName;
    try {
        result.passed = testFunc();
        result.message = result.passed ? "PASSED" : "FAILED";
        if (result.passed) {
            passedTests++;
        }
    } catch (const std::exception& e) {
        result.passed = false;
        result.message = std::string("EXCEPTION: ") + e.what();
    }
    results.push_back(result);
}

void TestFramework::runAllTests() {
    // Tests are already run when added
}

void TestFramework::printResults() const {
    std::cout << "\n========== TEST RESULTS ==========" << std::endl;
    for (const auto& result : results) {
        std::cout << (result.passed ? "[PASS] " : "[FAIL] ") << result.testName << std::endl;
        if (!result.message.empty()) {
            std::cout << "  " << result.message << std::endl;
        }
    }
    std::cout << "=================================" << std::endl;
    std::cout << "Total: " << totalTests << ", Passed: " << passedTests << ", Failed: " << (totalTests - passedTests) << std::endl;
}

int TestFramework::getTotalTests() const {
    return totalTests;
}

int TestFramework::getPassedTests() const {
    return passedTests;
}

bool TestFramework::allTestsPassed() const {
    return totalTests == passedTests;
}

} // namespace asn1::utils
