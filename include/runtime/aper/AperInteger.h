#pragma once
#include "runtime/core/BitWriter.h"
#include "runtime/core/BitReader.h"
#include <cstdint>

namespace asn1::runtime {

class AperInteger {
public:
    // Constrained integer WITHOUT extension marker (ext bit not included).
    static void encodeConstrainedInt(BitWriter& writer, int64_t value, int64_t minVal, int64_t maxVal);
    static int64_t decodeConstrainedInt(BitReader& reader, int64_t minVal, int64_t maxVal);

    // Constrained integer WITH extension marker present in the type.
    // The extension bit is packed into the encoding:
    //   - small range (≤256): [ext=0][value_bits...][padding] in one byte
    //   - medium range (≤65536): [ext=0][7 padding bits] then 2 value bytes
    //   - large range (>65536): [(ext<<7)|((byteCount-1)<<4)|0000] then byteCount value bytes
    // isExtended is always false for encoding in root range; true path not yet implemented.
    static void encodeConstrainedIntExt(BitWriter& writer, int64_t value, int64_t minVal, int64_t maxVal);
    // Returns the decoded value; sets isExtended=true when the extension bit was 1.
    static int64_t decodeConstrainedIntExt(BitReader& reader, int64_t minVal, int64_t maxVal, bool& isExtended);

    static void encodeUnconstrainedInt(BitWriter& writer, int64_t value);
    static int64_t decodeUnconstrainedInt(BitReader& reader);
};

} // namespace asn1::runtime
