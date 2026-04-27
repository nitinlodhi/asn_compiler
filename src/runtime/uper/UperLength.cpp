#include "runtime/uper/UperLength.h"
#include "runtime/uper/RangeUtils.h"
#include <stdexcept>

namespace asn1::runtime {

void UperLength::encodeLength(BitWriter& writer, size_t length, 
                            size_t minLength, size_t maxLength) {
    if (minLength == maxLength) {
        // Fixed length, nothing to encode
        return;
    }
    
    int64_t range = maxLength - minLength + 1;
    if (range <= 1) {
        return;
    }
    
    int bits = RangeUtils::calculateRangeBits(minLength, maxLength);
    int64_t normalized = length - minLength;
    writer.writeBits(normalized, bits);
}

size_t UperLength::decodeLength(BitReader& reader, 
                              size_t minLength, size_t maxLength) {
    if (minLength == maxLength) {
        return minLength;
    }
    
    int64_t range = maxLength - minLength + 1;
    if (range <= 1) {
        return minLength;
    }
    
    int bits = RangeUtils::calculateRangeBits(minLength, maxLength);
    int64_t normalized = reader.readBits(bits);
    return minLength + normalized;
}

void UperLength::encodeUnconstrainedLength(BitWriter& writer, size_t length) {
    if (length < 128) {
        // length is encoded in one octet, with MSB = 0
        writer.writeBits(length, 8);
    } else if (length < 16384) {
        // length is encoded in two octets, with first two bits = 10...
        uint16_t value = (0b10 << 14) | length;
        writer.writeBits(value, 16);
    } else {
        // Fragmentation (X.691, 10.9.3.8)
        size_t m = (length + 16383) / 16384;
        if (m > 64) {
            throw std::runtime_error("Unconstrained length > 1,048,576 is not supported.");
        }
        uint8_t value = 0xC0 | (m - 1); // m is 1-64, so m-1 is 0-63
        writer.writeBits(value, 8);
    }
}

size_t UperLength::decodeUnconstrainedLength(BitReader& reader) {
    uint8_t first_byte = reader.readByte();
    if ((first_byte & 0x80) == 0) { // MSB is 0
        return first_byte;
    } else if ((first_byte & 0xC0) == 0x80) { // MSBs are 10
        uint8_t second_byte = reader.readByte();
        return ((first_byte & 0x3F) << 8) | second_byte;
    } else { // MSBs are 11, fragmentation
        size_t multiplier = (first_byte & 0x3F) + 1; // m-1 was encoded, so add 1 back
        return multiplier * 16384;
    }
}

} // namespace asn1::runtime
