#ifndef ASN1_TESTS_3GPP_HEX_TESTS_H
#define ASN1_TESTS_3GPP_HEX_TESTS_H

#include <string>
#include <vector>
#include "utils/TestFramework.h"

namespace asn1::tests {

class ThreeGppHexTests {
public:
    static void register_tests(utils::TestFramework& runner);

private:
    static bool verifyAgainstHex(const std::string& schema, const std::string& inputHex, 
                         const std::string& expectedHex);
    static bool testIntegerConstraints();
    static bool testSequenceExtensions();
};

} // namespace asn1::tests

#endif // ASN1_TESTS_3GPP_HEX_TESTS_H