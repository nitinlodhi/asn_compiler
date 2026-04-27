#include "runtime/uper/UperInteger.h"
#include "runtime/uper/RangeUtils.h"
#include "runtime/uper/UperLength.h"
#include <stdexcept>

namespace asn1::runtime {

void UperInteger::encodeConstrainedInt(BitWriter& writer, int64_t value, 
                                      int64_t minValue, int64_t maxValue) {
    int64_t normalized = RangeUtils::normalizeValue(value, minValue);
    int bits = RangeUtils::calculateRangeBits(minValue, maxValue);
    if (bits > 0) {
        writer.writeBits(normalized, bits);
    }
}

int64_t UperInteger::decodeConstrainedInt(BitReader& reader, 
                                         int64_t minValue, int64_t maxValue) {
    int bits = RangeUtils::calculateRangeBits(minValue, maxValue);
    if (bits <= 0) {
        return minValue;
    }
    
    int64_t normalized = reader.readBits(bits);
    return RangeUtils::denormalizeValue(normalized, minValue);
}

void UperInteger::encodeNormallySmallInt(BitWriter& writer, int64_t value) {
    if (value >= 0 && value < 64) {
        writer.writeBits(0, 1); // flag bit
        writer.writeBits(value, 6);
    } else {
        writer.writeBits(1, 1); // flag bit
        encodeUnconstrainedInt(writer, value);
    }
}

int64_t UperInteger::decodeNormallySmallInt(BitReader& reader) {
    bool is_large = reader.readBits(1) != 0;
    if (!is_large) {
        return reader.readBits(6);
    }
    return decodeUnconstrainedInt(reader);
}

void UperInteger::encodeUnconstrainedInt(BitWriter& writer, int64_t value) {
    size_t num_bytes;
    if (value >= 0) {
        uint64_t temp = value;
        num_bytes = 1;
        while (temp > 0x7F) {
            temp >>= 8;
            num_bytes++;
        }
    } else { // value < 0
        int64_t temp = value;
        num_bytes = 1;
        while (temp < -128) {
            temp >>= 8;
            num_bytes++;
        }
    }

    UperLength::encodeUnconstrainedLength(writer, num_bytes);
    for (int i = static_cast<int>(num_bytes) - 1; i >= 0; --i) {
        writer.writeByte((value >> (i * 8)) & 0xFF);
    }
}

int64_t UperInteger::decodeUnconstrainedInt(BitReader& reader) {
    size_t num_bytes = UperLength::decodeUnconstrainedLength(reader);
    if (num_bytes == 0) {
        return 0;
    }
    if (num_bytes > 8) {
        throw std::runtime_error("Unconstrained integer length > 8 octets is not supported.");
    }

    uint64_t unsigned_val = 0;
    for (size_t i = 0; i < num_bytes; ++i) {
        unsigned_val = (unsigned_val << 8) | reader.readByte();
    }

    // Sign-extend
    int shift = 64 - (num_bytes * 8);
    return static_cast<int64_t>(unsigned_val << shift) >> shift;
}

} // namespace asn1::runtime
