#include "runtime/core/BitWriter.h"
#include "runtime/core/BitUtils.h"

namespace asn1::runtime {

BitWriter::BitWriter() : bitOffset(0) {}

void BitWriter::writeBits(uint64_t value, int numBits) {
    for (int i = numBits - 1; i >= 0; i--) {
        bool bit = (value >> i) & 1;
        
        size_t byteIndex = bitOffset / 8;
        int bitPosition = 7 - (bitOffset % 8);
        
        if (byteIndex >= buffer.size()) {
            buffer.push_back(0);
        }
        
        BitUtils::setBit(buffer[byteIndex], bitPosition, bit);
        bitOffset++;
    }
}

void BitWriter::writeByte(uint8_t value) {
    writeBits(value, 8);
}

void BitWriter::writeBytes(const uint8_t* data, size_t num_bits) {
    size_t num_bytes_to_read = (num_bits + 7) / 8;
    for (size_t i = 0; i < num_bytes_to_read; ++i) {
        size_t bits_in_this_byte = (i == num_bytes_to_read - 1 && num_bits % 8 != 0) ? num_bits % 8 : 8;
        if (bits_in_this_byte > 0) {
            // We need to write the most significant `bits_in_this_byte` from data[i]
            uint8_t value_to_write = data[i] >> (8 - bits_in_this_byte);
            writeBits(value_to_write, bits_in_this_byte);
        }
    }
}

void BitWriter::alignToOctet() {
    if (bitOffset % 8 != 0) {
        bitOffset += 8 - (bitOffset % 8);
    }
}

const uint8_t* BitWriter::getBuffer() const {
    return buffer.data();
}

size_t BitWriter::getBufferSize() const {
    return (bitOffset + 7) / 8;
}

size_t BitWriter::getBitOffset() const {
    return bitOffset;
}

void BitWriter::reset() {
    buffer.clear();
    bitOffset = 0;
}

} // namespace asn1::runtime
