#include "runtime/c/asn1_uper.h"
#include <math.h>
#include <stdint.h>
#include <stddef.h>

/* ── Range helpers ─────────────────────────────────────────────────────── */

int asn1_range_bits(int64_t min_val, int64_t max_val) {
    if (min_val >= max_val) return 0;
    int64_t range = max_val - min_val + 1;
    if (range <= 0) return 0;
    int bits = 0;
    int64_t v = range - 1;
    while (v > 0) { bits++; v >>= 1; }
    return bits;
}

/* ── Constrained integer ───────────────────────────────────────────────── */

void asn1_uper_encode_constrained_int(Asn1BitWriter* bw, int64_t value,
                                       int64_t min_val, int64_t max_val) {
    int bits = asn1_range_bits(min_val, max_val);
    if (bits > 0)
        asn1_bw_write_bits(bw, (uint64_t)(value - min_val), bits);
}

int64_t asn1_uper_decode_constrained_int(Asn1BitReader* br,
                                          int64_t min_val, int64_t max_val) {
    int bits = asn1_range_bits(min_val, max_val);
    if (bits <= 0) return min_val;
    return min_val + (int64_t)asn1_br_read_bits(br, bits);
}

/* ── Unconstrained integer ─────────────────────────────────────────────── */

void asn1_uper_encode_unconstrained_int(Asn1BitWriter* bw, int64_t value) {
    size_t num_bytes;
    if (value >= 0) {
        uint64_t t = (uint64_t)value;
        num_bytes = 1;
        while (t > 0x7F) { t >>= 8; num_bytes++; }
    } else {
        int64_t t = value;
        num_bytes = 1;
        while (t < -128) { t >>= 8; num_bytes++; }
    }
    asn1_uper_encode_unconstrained_length(bw, num_bytes);
    for (int i = (int)num_bytes - 1; i >= 0; --i)
        asn1_bw_write_byte(bw, (uint8_t)((value >> (i * 8)) & 0xFF));
}

int64_t asn1_uper_decode_unconstrained_int(Asn1BitReader* br) {
    size_t num_bytes = asn1_uper_decode_unconstrained_length(br);
    if (num_bytes == 0) return 0;
    uint64_t uval = 0;
    for (size_t i = 0; i < num_bytes; ++i)
        uval = (uval << 8) | asn1_br_read_byte(br);
    /* sign-extend */
    int shift = 64 - (int)(num_bytes * 8);
    return (int64_t)(uval << shift) >> shift;
}

/* ── Constrained length ────────────────────────────────────────────────── */

void asn1_uper_encode_length(Asn1BitWriter* bw, size_t length,
                              size_t min_len, size_t max_len) {
    if (min_len == max_len) return; /* fixed — nothing to encode */
    int bits = asn1_range_bits((int64_t)min_len, (int64_t)max_len);
    asn1_bw_write_bits(bw, (uint64_t)(length - min_len), bits);
}

size_t asn1_uper_decode_length(Asn1BitReader* br,
                                size_t min_len, size_t max_len) {
    if (min_len == max_len) return min_len;
    int bits = asn1_range_bits((int64_t)min_len, (int64_t)max_len);
    return min_len + (size_t)asn1_br_read_bits(br, bits);
}

/* ── Unconstrained length (X.691 §10.9) ───────────────────────────────── */

void asn1_uper_encode_unconstrained_length(Asn1BitWriter* bw, size_t length) {
    if (length < 128) {
        asn1_bw_write_bits(bw, length, 8);
    } else if (length < 16384) {
        asn1_bw_write_bits(bw, (uint64_t)(0x8000u | length), 16);
    } else {
        /* fragmentation — encode multiplier */
        size_t m = (length + 16383) / 16384;
        asn1_bw_write_bits(bw, (uint64_t)(0xC0u | (m - 1)), 8);
    }
}

size_t asn1_uper_decode_unconstrained_length(Asn1BitReader* br) {
    uint8_t first = asn1_br_read_byte(br);
    if ((first & 0x80u) == 0) return first;
    if ((first & 0xC0u) == 0x80u) {
        uint8_t second = asn1_br_read_byte(br);
        return (size_t)((first & 0x3Fu) << 8) | second;
    }
    return ((size_t)(first & 0x3Fu) + 1) * 16384; /* fragmented */
}

/* ── CHOICE index ──────────────────────────────────────────────────────── */

void asn1_uper_encode_choice_index(Asn1BitWriter* bw, int idx, int num_choices) {
    if (num_choices <= 1) return;
    int bits = asn1_range_bits(0, num_choices - 1);
    asn1_bw_write_bits(bw, (uint64_t)idx, bits);
}

int asn1_uper_decode_choice_index(Asn1BitReader* br, int num_choices) {
    if (num_choices <= 1) return 0;
    int bits = asn1_range_bits(0, num_choices - 1);
    return (int)asn1_br_read_bits(br, bits);
}

void asn1_uper_encode_ext_choice_index(Asn1BitWriter* bw, int idx, int num_base) {
    if (idx < num_base) {
        asn1_bw_write_bits(bw, 0, 1); /* not extended */
        asn1_uper_encode_choice_index(bw, idx, num_base);
    } else {
        asn1_bw_write_bits(bw, 1, 1); /* extended — skip content */
    }
}

int asn1_uper_decode_ext_choice_index(Asn1BitReader* br, int num_base) {
    int is_ext = (int)asn1_br_read_bits(br, 1);
    if (!is_ext) return asn1_uper_decode_choice_index(br, num_base);
    return -1; /* extended alternative — caller must skip open type */
}

/* ── SEQUENCE preamble ─────────────────────────────────────────────────── */

void asn1_uper_encode_seq_preamble(Asn1BitWriter* bw,
                                    uint64_t bitmap, int opt_count) {
    if (opt_count > 0)
        asn1_bw_write_bits(bw, bitmap, opt_count);
}

uint64_t asn1_uper_decode_seq_preamble(Asn1BitReader* br, int opt_count) {
    if (opt_count <= 0) return 0;
    return asn1_br_read_bits(br, opt_count);
}

/* ── Extension marker ──────────────────────────────────────────────────── */

void asn1_uper_encode_ext_marker(Asn1BitWriter* bw, int is_extended) {
    asn1_bw_write_bits(bw, is_extended ? 1 : 0, 1);
}

int asn1_uper_decode_ext_marker(Asn1BitReader* br) {
    return (int)asn1_br_read_bits(br, 1);
}

/* ── Open type skip ────────────────────────────────────────────────────── */

void asn1_uper_skip_open_type(Asn1BitReader* br) {
    size_t len = asn1_uper_decode_unconstrained_length(br);
    asn1_br_align_to_octet(br);
    asn1_br_skip(br, (int)(len * 8));
}
