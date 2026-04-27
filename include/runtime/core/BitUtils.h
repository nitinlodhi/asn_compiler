#ifndef ASN1_RUNTIME_BIT_UTILS_H
#define ASN1_RUNTIME_BIT_UTILS_H

#include <cstddef>
#include <cstdint>

namespace asn1::runtime {

class BitUtils {
public:
    // Bit manipulation utilities
    static uint8_t getBit(uint8_t byte, int position);
    static void setBit(uint8_t& byte, int position, bool value);
    static int countBits(uint64_t value);
    static uint64_t maskBits(int count);
    static int bytesNeeded(int bitCount);
};

} // namespace asn1::runtime

#endif // ASN1_RUNTIME_BIT_UTILS_H
