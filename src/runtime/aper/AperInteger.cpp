#include "runtime/aper/AperInteger.h"
#include "runtime/uper/RangeUtils.h"
#include <stdexcept>

namespace asn1::runtime {

void AperInteger::encodeConstrainedInt(BitWriter& writer, int64_t value, int64_t minVal, int64_t maxVal) {
    int64_t numValues = maxVal - minVal + 1;
    int64_t normalized = value - minVal;

    if (numValues == 1) {
        return; // zero bits
    }

    writer.alignToOctet();

    if (numValues <= 256) {
        int bits = RangeUtils::calculateRangeBits(minVal, maxVal);
        writer.writeBits(normalized, bits);
        writer.alignToOctet();
    } else if (numValues <= 65536) {
        writer.writeBits((normalized >> 8) & 0xFF, 8);
        writer.writeBits(normalized & 0xFF, 8);
    } else {
        int byteCount;
        if (normalized < (1LL << 8)) byteCount = 1;
        else if (normalized < (1LL << 16)) byteCount = 2;
        else if (normalized < (1LL << 24)) byteCount = 3;
        else byteCount = 4;

        writer.writeBits((byteCount - 1) << 6, 8);
        for (int i = byteCount - 1; i >= 0; i--) {
            writer.writeBits((normalized >> (i * 8)) & 0xFF, 8);
        }
    }
}

int64_t AperInteger::decodeConstrainedInt(BitReader& reader, int64_t minVal, int64_t maxVal) {
    int64_t numValues = maxVal - minVal + 1;

    if (numValues == 1) {
        return minVal;
    }

    reader.alignToOctet();

    int64_t normalized;
    if (numValues <= 256) {
        int bits = RangeUtils::calculateRangeBits(minVal, maxVal);
        normalized = reader.readBits(bits);
        reader.alignToOctet();
    } else if (numValues <= 65536) {
        int64_t hi = reader.readBits(8);
        int64_t lo = reader.readBits(8);
        normalized = (hi << 8) | lo;
    } else {
        int indicator = static_cast<int>(reader.readBits(8));
        int byteCount = (indicator >> 6) + 1;
        normalized = 0;
        for (int i = 0; i < byteCount; i++) {
            normalized = (normalized << 8) | reader.readBits(8);
        }
    }

    return minVal + normalized;
}

void AperInteger::encodeUnconstrainedInt(BitWriter& writer, int64_t value) {
    writer.alignToOctet();
    int byteCount;
    if (value >= -128 && value <= 127) byteCount = 1;
    else if (value >= -32768 && value <= 32767) byteCount = 2;
    else if (value >= -8388608 && value <= 8388607) byteCount = 3;
    else if (value >= -2147483648LL && value <= 2147483647LL) byteCount = 4;
    else byteCount = 8;

    writer.writeBits(byteCount, 8);
    for (int i = byteCount - 1; i >= 0; i--) {
        writer.writeBits((value >> (i * 8)) & 0xFF, 8);
    }
}

int64_t AperInteger::decodeUnconstrainedInt(BitReader& reader) {
    reader.alignToOctet();
    int byteCount = static_cast<int>(reader.readBits(8));
    int64_t value = 0;
    for (int i = 0; i < byteCount; i++) {
        value = (value << 8) | reader.readBits(8);
    }
    // Sign-extend
    int signBit = byteCount * 8 - 1;
    if (byteCount < 8 && (value >> signBit) & 1) {
        value |= -(1LL << (byteCount * 8));
    }
    return value;
}

} // namespace asn1::runtime
