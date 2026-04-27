#ifndef ASN1_RUNTIME_UPER_SEQUENCE_H
#define ASN1_RUNTIME_UPER_SEQUENCE_H

#include <cstdint>
#include "runtime/core/BitWriter.h"
#include "runtime/core/BitReader.h"

namespace asn1::runtime {

class UperSequence {
public:
    // Encode/Decode SEQUENCE preamble (presence bitmap for optional fields)
    static void encodeSequencePreamble(BitWriter& writer, uint64_t presenceBitmap, 
                                      int optionalFieldCount);
    static uint64_t decodeSequencePreamble(BitReader& reader, int optionalFieldCount);
    
    // Extension addition bit
    static void encodeExtensionAddition(BitWriter& writer, bool hasExtensions);
    static bool decodeExtensionAddition(BitReader& reader);
};

} // namespace asn1::runtime

#endif // ASN1_RUNTIME_UPER_SEQUENCE_H
