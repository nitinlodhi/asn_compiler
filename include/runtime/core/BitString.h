#ifndef ASN1_RUNTIME_BIT_STRING_H
#define ASN1_RUNTIME_BIT_STRING_H

#include <vector>
#include <cstdint>

namespace asn1::runtime {

struct BitString {
    std::vector<uint8_t> data;
    size_t bit_length = 0;
};

} // namespace asn1::runtime

#endif // ASN1_RUNTIME_BIT_STRING_H