#ifndef ASN1_RUNTIME_UPER_EXTENSION_H
#define ASN1_RUNTIME_UPER_EXTENSION_H

#include <cstdint>
#include "runtime/core/BitString.h"
#include "runtime/core/BitWriter.h"
#include "runtime/core/BitReader.h"

namespace asn1::runtime {

class UperExtension {
public:
    // Extension marker (... in ASN.1)
    static void encodeExtensionMarker(BitWriter& writer, bool isExtended);
    static bool decodeExtensionMarker(BitReader& reader);
    
    // Open type (length-prefixed bit string for extension additions)
    static void encodeOpenType(BitWriter& writer, const BitString& open_type_data);
    static BitString decodeOpenType(BitReader& reader);
};

} // namespace asn1::runtime

#endif // ASN1_RUNTIME_UPER_EXTENSION_H
