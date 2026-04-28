#include "runtime/c/asn1_bitreader.h"

void asn1_br_init(Asn1BitReader* br, const uint8_t* buffer, size_t size) {
    br->buffer      = buffer;
    br->buffer_size = size;
    br->bit_offset  = 0;
}

uint64_t asn1_br_read_bits(Asn1BitReader* br, int num_bits) {
    uint64_t result = 0;
    for (int i = 0; i < num_bits; ++i) {
        size_t byte_idx = br->bit_offset / 8;
        int    bit_pos  = 7 - (int)(br->bit_offset % 8);
        if (byte_idx >= br->buffer_size) break;
        uint8_t bit = (br->buffer[byte_idx] >> bit_pos) & 1u;
        result = (result << 1) | bit;
        br->bit_offset++;
    }
    return result;
}

uint8_t asn1_br_read_byte(Asn1BitReader* br) {
    return (uint8_t)asn1_br_read_bits(br, 8);
}

void asn1_br_read_bytes(Asn1BitReader* br, uint8_t* data, size_t num_bits) {
    size_t num_bytes = (num_bits + 7) / 8;
    for (size_t i = 0; i < num_bytes; ++i) {
        size_t bits = (i == num_bytes - 1 && num_bits % 8 != 0) ? num_bits % 8 : 8;
        data[i] = bits > 0
            ? (uint8_t)(asn1_br_read_bits(br, (int)bits) << (8 - bits))
            : 0;
    }
}

void asn1_br_align_to_octet(Asn1BitReader* br) {
    if (br->bit_offset % 8 != 0)
        br->bit_offset += 8 - (br->bit_offset % 8);
}

void asn1_br_skip(Asn1BitReader* br, int num_bits) {
    br->bit_offset += (size_t)num_bits;
}

size_t asn1_br_get_bit_offset(const Asn1BitReader* br) {
    return br->bit_offset;
}

int asn1_br_is_at_end(const Asn1BitReader* br) {
    return br->bit_offset >= br->buffer_size * 8;
}
