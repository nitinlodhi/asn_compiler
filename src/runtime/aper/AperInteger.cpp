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

// Extension-marker variant: packs the extension bit (always 0 = root) into the
// same byte as the length/value indicator, matching the APER wire format:
//   small range (≤256):  [0][val_bits][padding]  — one byte
//   medium range (≤64K): [0][7 padding bits] then 2 value bytes
//   large range:         [(0<<7)|((byteCount-1)<<4)|0000] then byteCount bytes
void AperInteger::encodeConstrainedIntExt(BitWriter& writer, int64_t value, int64_t minVal, int64_t maxVal) {
    int64_t numValues = maxVal - minVal + 1;
    int64_t normalized = value - minVal;

    if (numValues == 1) {
        writer.writeBits(0, 1); // ext bit only
        writer.alignToOctet();
        return;
    }

    if (numValues <= 256) {
        // ext=0 bit followed immediately by value bits, then pad to byte boundary
        int bits = RangeUtils::calculateRangeBits(minVal, maxVal);
        writer.writeBits(0, 1);           // extension bit = 0
        writer.writeBits(normalized, bits);
        writer.alignToOctet();
    } else if (numValues <= 65536) {
        // ext=0 byte (ext bit + 7 padding), then 2 value bytes
        writer.writeBits(0, 1);
        writer.alignToOctet();
        writer.writeBits((normalized >> 8) & 0xFF, 8);
        writer.writeBits(normalized & 0xFF, 8);
    } else {
        int byteCount;
        if (normalized < (1LL << 8)) byteCount = 1;
        else if (normalized < (1LL << 16)) byteCount = 2;
        else if (normalized < (1LL << 24)) byteCount = 3;
        else byteCount = 4;

        // Single header byte: [ext=0 | (byteCount-1) in bits 6-4 | 4 zero bits]
        writer.writeBits(((byteCount - 1) << 4), 8);
        for (int i = byteCount - 1; i >= 0; i--) {
            writer.writeBits((normalized >> (i * 8)) & 0xFF, 8);
        }
    }
}

int64_t AperInteger::decodeConstrainedIntExt(BitReader& reader, int64_t minVal, int64_t maxVal, bool& isExtended) {
    int64_t numValues = maxVal - minVal + 1;

    if (numValues == 1) {
        isExtended = (reader.readBits(1) != 0);
        reader.alignToOctet();
        return minVal;
    }

    if (numValues <= 256) {
        int bits = RangeUtils::calculateRangeBits(minVal, maxVal);
        isExtended = (reader.readBits(1) != 0);
        int64_t normalized = reader.readBits(bits);
        reader.alignToOctet();
        return minVal + normalized;
    } else if (numValues <= 65536) {
        isExtended = (reader.readBits(1) != 0);
        reader.alignToOctet(); // skip 7 padding bits
        int64_t hi = reader.readBits(8);
        int64_t lo = reader.readBits(8);
        return minVal + ((hi << 8) | lo);
    } else {
        int header = static_cast<int>(reader.readBits(8));
        isExtended = (header >> 7) & 1;
        int byteCount = ((header >> 4) & 0x7) + 1; // bits 6-4
        int64_t normalized = 0;
        for (int i = 0; i < byteCount; i++) {
            normalized = (normalized << 8) | reader.readBits(8);
        }
        return minVal + normalized;
    }
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
