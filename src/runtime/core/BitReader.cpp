#include "runtime/core/BitReader.h"
#include "runtime/core/BitUtils.h"

namespace asn1::runtime {

BitReader::BitReader(const uint8_t* buffer, size_t bufferSize)
    : buffer(buffer), bufferSize(bufferSize), bitOffset(0) {}

uint64_t BitReader::readBits(int numBits) {
    uint64_t result = 0;
    
    for (int i = 0; i < numBits; i++) {
        size_t byteIndex = bitOffset / 8;
        int bitPosition = 7 - (bitOffset % 8);
        
        if (byteIndex >= bufferSize) {
            break; // End of buffer
        }
        
        uint8_t bit = BitUtils::getBit(buffer[byteIndex], bitPosition);
        result = (result << 1) | bit;
        bitOffset++;
    }
    
    return result;
}

uint8_t BitReader::readByte() {
    return static_cast<uint8_t>(readBits(8));
}

void BitReader::readBytes(uint8_t* data, size_t num_bits) {
    size_t num_bytes_to_read = (num_bits + 7) / 8;
    for (size_t i = 0; i < num_bytes_to_read; ++i) {
        size_t bits_in_this_byte = (i == num_bytes_to_read - 1 && num_bits % 8 != 0) ? num_bits % 8 : 8;
        if (bits_in_this_byte > 0) {
            // Read bits and align them to the MSB of the byte
            data[i] = static_cast<uint8_t>(readBits(bits_in_this_byte) << (8 - bits_in_this_byte));
        } else {
            data[i] = 0;
        }
    }
}

void BitReader::readBytes(std::vector<uint8_t>& data, size_t num_bits) {
    size_t num_bytes_to_read = (num_bits + 7) / 8;
    data.resize(num_bytes_to_read);
    readBytes(data.data(), num_bits);
}

void BitReader::alignToOctet() {
    if (bitOffset % 8 != 0) {
        bitOffset += 8 - (bitOffset % 8);
    }
}

void BitReader::skip(int numBits) {
    bitOffset += numBits;
}

size_t BitReader::getBitOffset() const {
    return bitOffset;
}

bool BitReader::isAtEnd() const {
    return bitOffset >= bufferSize * 8;
}

void BitReader::reset() {
    bitOffset = 0;
}

} // namespace asn1::runtime
