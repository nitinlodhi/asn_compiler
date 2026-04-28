#ifndef ASN1_RUNTIME_C_UPER_H
#define ASN1_RUNTIME_C_UPER_H

#include <stddef.h>
#include <stdint.h>
#include "asn1_bitwriter.h"
#include "asn1_bitreader.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Number of bits needed to encode values in [min_val, max_val] */
int asn1_range_bits(int64_t min_val, int64_t max_val);

/* Constrained integer (X.691 §12.2) */
void    asn1_uper_encode_constrained_int(Asn1BitWriter* bw, int64_t value,
                                          int64_t min_val, int64_t max_val);
int64_t asn1_uper_decode_constrained_int(Asn1BitReader* br,
                                          int64_t min_val, int64_t max_val);

/* Unconstrained integer */
void    asn1_uper_encode_unconstrained_int(Asn1BitWriter* bw, int64_t value);
int64_t asn1_uper_decode_unconstrained_int(Asn1BitReader* br);

/* Constrained length determinant (X.691 §12.2 applied to length) */
void   asn1_uper_encode_length(Asn1BitWriter* bw, size_t length,
                                size_t min_len, size_t max_len);
size_t asn1_uper_decode_length(Asn1BitReader* br,
                                size_t min_len, size_t max_len);

/* Unconstrained length determinant (X.691 §10.9) */
void   asn1_uper_encode_unconstrained_length(Asn1BitWriter* bw, size_t length);
size_t asn1_uper_decode_unconstrained_length(Asn1BitReader* br);

/* CHOICE index (X.691 §22) */
void asn1_uper_encode_choice_index(Asn1BitWriter* bw, int idx, int num_choices);
int  asn1_uper_decode_choice_index(Asn1BitReader* br, int num_choices);

/* Extended CHOICE index (with leading extension bit) */
void asn1_uper_encode_ext_choice_index(Asn1BitWriter* bw, int idx, int num_base);
int  asn1_uper_decode_ext_choice_index(Asn1BitReader* br, int num_base);

/* SEQUENCE optional-field bitmap */
void     asn1_uper_encode_seq_preamble(Asn1BitWriter* bw,
                                        uint64_t bitmap, int opt_count);
uint64_t asn1_uper_decode_seq_preamble(Asn1BitReader* br, int opt_count);

/* Extension marker (single bit) */
void asn1_uper_encode_ext_marker(Asn1BitWriter* bw, int is_extended);
int  asn1_uper_decode_ext_marker(Asn1BitReader* br);

/* Skip an unknown open-type extension addition blob */
void asn1_uper_skip_open_type(Asn1BitReader* br);

#ifdef __cplusplus
}
#endif

#endif /* ASN1_RUNTIME_C_UPER_H */
