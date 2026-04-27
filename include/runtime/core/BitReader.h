#ifndef ASN1_RUNTIME_BIT_READER_H
#define ASN1_RUNTIME_BIT_READER_H

#include <cstdint>
#include <vector>
#include <memory>

namespace asn1::runtime {

class BitReader {
private:
    const uint8_t* buffer;
    size_t bufferSize;
    size_t bitOffset;

public:
    BitReader(const uint8_t* buffer, size_t bufferSize);
    ~BitReader() = default;

    uint64_t readBits(int numBits);
    uint8_t readByte();
    void readBytes(uint8_t* data, size_t num_bits);
    void readBytes(std::vector<uint8_t>& data, size_t num_bits);
    void alignToOctet();
    void skip(int numBits);
    
    size_t getBitOffset() const;
    bool isAtEnd() const;
    
    void reset();
};

using BitReaderPtr = std::shared_ptr<BitReader>;

} // namespace asn1::runtime

#endif // ASN1_RUNTIME_BIT_READER_H
