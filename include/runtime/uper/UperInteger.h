#ifndef ASN1_RUNTIME_UPER_INTEGER_H
#define ASN1_RUNTIME_UPER_INTEGER_H

#include <cstdint>
#include <optional>
#include "runtime/core/BitWriter.h"
#include "runtime/core/BitReader.h"

namespace asn1::runtime {

class UperInteger {
public:
    // Encode/Decode constrained integers
    static void encodeConstrainedInt(BitWriter& writer, int64_t value, 
                                    int64_t minValue, int64_t maxValue);
    static int64_t decodeConstrainedInt(BitReader& reader, 
                                       int64_t minValue, int64_t maxValue);
    
    // Normally small integers (semi-constrained)
    static void encodeNormallySmallInt(BitWriter& writer, int64_t value);
    static int64_t decodeNormallySmallInt(BitReader& reader);
    
    // Unconstrained integers
    static void encodeUnconstrainedInt(BitWriter& writer, int64_t value);
    static int64_t decodeUnconstrainedInt(BitReader& reader);
};

} // namespace asn1::runtime

#endif // ASN1_RUNTIME_UPER_INTEGER_H
