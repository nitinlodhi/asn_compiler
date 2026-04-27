#ifndef ASN1_RUNTIME_UPER_LENGTH_H
#define ASN1_RUNTIME_UPER_LENGTH_H

#include <cstdint>
#include "runtime/core/BitWriter.h"
#include "runtime/core/BitReader.h"

namespace asn1::runtime {

class UperLength {
public:
    // Encode/Decode lengths
    static void encodeLength(BitWriter& writer, size_t length, 
                           size_t minLength, size_t maxLength);
    static size_t decodeLength(BitReader& reader, 
                             size_t minLength, size_t maxLength);
    
    // Unconstrained length (X.691, 10.9)
    static void encodeUnconstrainedLength(BitWriter& writer, size_t length);
    static size_t decodeUnconstrainedLength(BitReader& reader);
};

} // namespace asn1::runtime

#endif // ASN1_RUNTIME_UPER_LENGTH_H
