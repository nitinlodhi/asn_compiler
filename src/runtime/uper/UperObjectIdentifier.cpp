#include "runtime/uper/UperObjectIdentifier.h"
#include "runtime/uper/UperLength.h"
#include <vector>
#include <stdexcept>

namespace asn1::runtime {

// Helper to encode a single component value into a temporary writer
static void encode_component(BitWriter& writer, uint64_t component) {
    if (component < 128) {
        writer.writeByte(component & 0x7F);
        return;
    }

    std::vector<uint8_t> octets;
    octets.push_back(component & 0x7F);
    component >>= 7;

    while (component > 0) {
        octets.push_back((component & 0x7F) | 0x80);
        component >>= 7;
    }
    
    for (auto it = octets.rbegin(); it != octets.rend(); ++it) {
        writer.writeByte(*it);
    }
}

// Helper to decode a single component value
static uint64_t decode_component(BitReader& reader) {
    uint64_t value = 0;
    uint8_t byte;
    do {
        if (reader.isAtEnd()) {
            throw std::runtime_error("Unexpected end of buffer while decoding OID component.");
        }
        byte = reader.readByte();
        value = (value << 7) | (byte & 0x7F);
    } while ((byte & 0x80) != 0);
    return value;
}


void UperObjectIdentifier::encode(BitWriter& writer, const ObjectIdentifier& oid) {
    if (oid.size() < 2) {
        throw std::runtime_error("OBJECT IDENTIFIER must have at least two components.");
    }

    BitWriter content_writer;
    
    // Encode first two components
    uint64_t first_val = oid[0] * 40 + oid[1];
    encode_component(content_writer, first_val);

    // Encode remaining components
    for (size_t i = 2; i < oid.size(); ++i) {
        encode_component(content_writer, oid[i]);
    }

    // Write length and then content
    UperLength::encodeUnconstrainedLength(writer, content_writer.getBufferSize());
    const uint8_t* content_buf = content_writer.getBuffer();
    for(size_t i = 0; i < content_writer.getBufferSize(); ++i) {
        writer.writeByte(content_buf[i]);
    }
}

void UperObjectIdentifier::decode(BitReader& reader, ObjectIdentifier& oid) {
    oid.clear();
    size_t length = UperLength::decodeUnconstrainedLength(reader);
    
    if (length == 0) {
        return; // Empty OID
    }

    // Create a temporary buffer for the content
    std::vector<uint8_t> content_buffer;
    content_buffer.resize(length);
    for(size_t i = 0; i < length; ++i) {
        content_buffer[i] = reader.readByte();
    }
    BitReader content_reader(content_buffer.data(), content_buffer.size());

    // Decode first value
    uint64_t first_val = decode_component(content_reader);
    oid.push_back(first_val / 40);
    oid.push_back(first_val % 40);

    // Decode remaining components
    while (!content_reader.isAtEnd()) {
        oid.push_back(decode_component(content_reader));
    }
}

} // namespace asn1::runtime