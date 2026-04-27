#ifndef ASN1_UTILS_TEST_FRAMEWORK_H
#define ASN1_UTILS_TEST_FRAMEWORK_H

#include <string>
#include <vector>
#include <functional>

namespace asn1::utils {

struct TestResult {
    std::string testName;
    bool passed;
    std::string message;
};

class TestFramework {
private:
    std::vector<TestResult> results;
    int totalTests;
    int passedTests;

public:
    TestFramework();
    ~TestFramework() = default;

    void addTest(const std::string& testName, std::function<bool()> testFunc);
    void runAllTests();
    void printResults() const;
    
    int getTotalTests() const;
    int getPassedTests() const;
    bool allTestsPassed() const;
};

} // namespace asn1::utils

#endif // ASN1_UTILS_TEST_FRAMEWORK_H
