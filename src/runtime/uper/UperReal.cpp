#include "runtime/uper/UperReal.h"
#include "runtime/uper/UperLength.h"
#include <cstring> // for memcpy
#include <stdexcept>
#include <algorithm> // for std::reverse

namespace asn1::runtime {

void UperReal::encode(BitWriter& writer, double value) {
    // UPER for REAL is commonly implemented as a length-prefixed binary representation (IEEE 754 double-precision)
    UperLength::encodeUnconstrainedLength(writer, sizeof(double));
    
    uint64_t bits;
    std::memcpy(&bits, &value, sizeof(double));

    // Write the 8 bytes of the double. UPER is big-endian.
    for (int i = 7; i >= 0; --i) {
        writer.writeByte((bits >> (i * 8)) & 0xFF);
    }
}

void UperReal::decode(BitReader& reader, double& value) {
    size_t length = UperLength::decodeUnconstrainedLength(reader);
    if (length != sizeof(double)) {
        throw std::runtime_error("UPER REAL decoding: expected length of " + std::to_string(sizeof(double)) + " but got " + std::to_string(length));
    }

    uint64_t bits = 0;
    for (size_t i = 0; i < sizeof(double); ++i) {
        bits = (bits << 8) | reader.readByte();
    }

    std::memcpy(&value, &bits, sizeof(double));
}

} // namespace asn1::runtime