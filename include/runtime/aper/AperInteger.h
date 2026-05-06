#pragma once
#include "runtime/core/BitWriter.h"
#include "runtime/core/BitReader.h"
#include <cstdint>

namespace asn1::runtime {

class AperInteger {
public:
    static void encodeConstrainedInt(BitWriter& writer, int64_t value, int64_t minVal, int64_t maxVal);
    static int64_t decodeConstrainedInt(BitReader& reader, int64_t minVal, int64_t maxVal);

    static void encodeUnconstrainedInt(BitWriter& writer, int64_t value);
    static int64_t decodeUnconstrainedInt(BitReader& reader);
};

} // namespace asn1::runtime
