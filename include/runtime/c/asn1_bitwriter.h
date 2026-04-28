#ifndef ASN1_RUNTIME_C_BITWRITER_H
#define ASN1_RUNTIME_C_BITWRITER_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint8_t* buffer;
    size_t   capacity;
    size_t   bit_offset;
} Asn1BitWriter;

void           asn1_bw_init(Asn1BitWriter* bw);
void           asn1_bw_free(Asn1BitWriter* bw);
void           asn1_bw_write_bits(Asn1BitWriter* bw, uint64_t value, int num_bits);
void           asn1_bw_write_byte(Asn1BitWriter* bw, uint8_t value);
void           asn1_bw_write_bytes(Asn1BitWriter* bw, const uint8_t* data, size_t num_bits);
void           asn1_bw_align_to_octet(Asn1BitWriter* bw);
const uint8_t* asn1_bw_get_buffer(const Asn1BitWriter* bw);
size_t         asn1_bw_get_buffer_size(const Asn1BitWriter* bw);
size_t         asn1_bw_get_bit_offset(const Asn1BitWriter* bw);

#ifdef __cplusplus
}
#endif

#endif /* ASN1_RUNTIME_C_BITWRITER_H */
