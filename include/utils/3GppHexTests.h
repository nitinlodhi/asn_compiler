#ifndef ASN1_UTILS_3GPP_HEX_TESTS_H
#define ASN1_UTILS_3GPP_HEX_TESTS_H

#include <string>
#include <vector>

namespace asn1::utils {

struct HexTestCase {
    std::string name;
    std::string asnSchema;
    std::string inputHex;
    std::string expectedOutputHex;
};

class ThreeGppHexTests {
private:
    std::vector<HexTestCase> testCases;

public:
    ThreeGppHexTests();
    ~ThreeGppHexTests() = default;

    void addTestCase(const HexTestCase& testCase);
    void runTests();
    
    bool verifyAgainstHex(const std::string& schema, const std::string& inputHex, 
                         const std::string& expectedHex);
    bool testIntegerConstraints();
    bool testSequenceExtensions();
};

} // namespace asn1::utils

#endif // ASN1_UTILS_3GPP_HEX_TESTS_H
