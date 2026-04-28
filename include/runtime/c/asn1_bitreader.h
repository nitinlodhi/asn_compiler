#ifndef ASN1_RUNTIME_C_BITREADER_H
#define ASN1_RUNTIME_C_BITREADER_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    const uint8_t* buffer;
    size_t         buffer_size;
    size_t         bit_offset;
} Asn1BitReader;

void     asn1_br_init(Asn1BitReader* br, const uint8_t* buffer, size_t size);
uint64_t asn1_br_read_bits(Asn1BitReader* br, int num_bits);
uint8_t  asn1_br_read_byte(Asn1BitReader* br);
void     asn1_br_read_bytes(Asn1BitReader* br, uint8_t* data, size_t num_bits);
void     asn1_br_align_to_octet(Asn1BitReader* br);
void     asn1_br_skip(Asn1BitReader* br, int num_bits);
size_t   asn1_br_get_bit_offset(const Asn1BitReader* br);
int      asn1_br_is_at_end(const Asn1BitReader* br);

#ifdef __cplusplus
}
#endif

#endif /* ASN1_RUNTIME_C_BITREADER_H */
