#include "runtime/core/BitUtils.h"
#include <algorithm>

namespace asn1::runtime {

uint8_t BitUtils::getBit(uint8_t byte, int position) {
    return (byte >> position) & 1;
}

void BitUtils::setBit(uint8_t& byte, int position, bool value) {
    if (value) {
        byte |= (1 << position);
    } else {
        byte &= ~(1 << position);
    }
}

int BitUtils::countBits(uint64_t value) {
    if (value == 0) return 0;
    int count = 0;
    while (value > 0) {
        count++;
        value >>= 1;
    }
    return count;
}

uint64_t BitUtils::maskBits(int count) {
    if (count >= 64) return ~0ULL;
    return (1ULL << count) - 1;
}

int BitUtils::bytesNeeded(int bitCount) {
    return (bitCount + 7) / 8;
}

} // namespace asn1::runtime
