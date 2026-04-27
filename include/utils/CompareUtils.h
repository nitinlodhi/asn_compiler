#ifndef ASN1_UTILS_COMPARE_UTILS_H
#define ASN1_UTILS_COMPARE_UTILS_H

#include <cstdint>
#include <vector>

namespace asn1::utils {

class CompareUtils {
public:
    static bool compareBuffers(const uint8_t* expected, const uint8_t* actual, size_t length);
    static std::string bufferToHex(const uint8_t* buffer, size_t length);
    static bool hexStringToBuffer(const std::string& hex, uint8_t* buffer, size_t& length);
};

} // namespace asn1::utils

#endif // ASN1_UTILS_COMPARE_UTILS_H
