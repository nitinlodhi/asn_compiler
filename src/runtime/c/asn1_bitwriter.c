#include "runtime/c/asn1_bitwriter.h"
#include <stdlib.h>
#include <string.h>

static void bw_ensure_capacity(Asn1BitWriter* bw) {
    size_t needed_bytes = (bw->bit_offset / 8) + 1;
    if (needed_bytes <= bw->capacity) return;
    size_t new_cap = bw->capacity == 0 ? 64 : bw->capacity * 2;
    while (new_cap < needed_bytes) new_cap *= 2;
    uint8_t* new_buf = (uint8_t*)realloc(bw->buffer, new_cap);
    if (!new_buf) abort(); /* OOM — acceptable for codec use */
    memset(new_buf + bw->capacity, 0, new_cap - bw->capacity);
    bw->buffer = new_buf;
    bw->capacity = new_cap;
}

void asn1_bw_init(Asn1BitWriter* bw) {
    bw->buffer     = NULL;
    bw->capacity   = 0;
    bw->bit_offset = 0;
}

void asn1_bw_free(Asn1BitWriter* bw) {
    free(bw->buffer);
    bw->buffer     = NULL;
    bw->capacity   = 0;
    bw->bit_offset = 0;
}

void asn1_bw_write_bits(Asn1BitWriter* bw, uint64_t value, int num_bits) {
    for (int i = num_bits - 1; i >= 0; --i) {
        int bit = (int)((value >> i) & 1u);
        bw_ensure_capacity(bw);
        size_t byte_idx = bw->bit_offset / 8;
        int    bit_pos  = 7 - (int)(bw->bit_offset % 8);
        if (bit)
            bw->buffer[byte_idx] |=  (uint8_t)(1u << bit_pos);
        else
            bw->buffer[byte_idx] &= (uint8_t)~(1u << bit_pos);
        bw->bit_offset++;
    }
}

void asn1_bw_write_byte(Asn1BitWriter* bw, uint8_t value) {
    asn1_bw_write_bits(bw, value, 8);
}

void asn1_bw_write_bytes(Asn1BitWriter* bw, const uint8_t* data, size_t num_bits) {
    size_t num_bytes = (num_bits + 7) / 8;
    for (size_t i = 0; i < num_bytes; ++i) {
        size_t bits = (i == num_bytes - 1 && num_bits % 8 != 0) ? num_bits % 8 : 8;
        if (bits > 0) {
            uint8_t v = (uint8_t)(data[i] >> (8 - bits));
            asn1_bw_write_bits(bw, v, (int)bits);
        }
    }
}

void asn1_bw_align_to_octet(Asn1BitWriter* bw) {
    if (bw->bit_offset % 8 != 0)
        bw->bit_offset += 8 - (bw->bit_offset % 8);
}

const uint8_t* asn1_bw_get_buffer(const Asn1BitWriter* bw) {
    return bw->buffer;
}

size_t asn1_bw_get_buffer_size(const Asn1BitWriter* bw) {
    return (bw->bit_offset + 7) / 8;
}

size_t asn1_bw_get_bit_offset(const Asn1BitWriter* bw) {
    return bw->bit_offset;
}
