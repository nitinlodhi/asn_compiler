#ifndef ASN1_RUNTIME_BIT_WRITER_H
#define ASN1_RUNTIME_BIT_WRITER_H

#include <cstdint>
#include <vector>
#include <memory>

namespace asn1::runtime {

class BitWriter {
private:
    std::vector<uint8_t> buffer;
    size_t bitOffset;

public:
    BitWriter();
    ~BitWriter() = default;

    void writeBits(uint64_t value, int numBits);
    void writeByte(uint8_t value);
    void writeBytes(const uint8_t* data, size_t num_bits);
    void alignToOctet();
    
    const uint8_t* getBuffer() const;
    size_t getBufferSize() const;
    size_t getBitOffset() const;
    
    void reset();
};

using BitWriterPtr = std::shared_ptr<BitWriter>;

} // namespace asn1::runtime

#endif // ASN1_RUNTIME_BIT_WRITER_H
