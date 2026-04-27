#ifndef ASN1_RUNTIME_UPER_OBJECT_IDENTIFIER_H
#define ASN1_RUNTIME_UPER_OBJECT_IDENTIFIER_H

#include "runtime/core/ObjectIdentifier.h"
#include "runtime/core/BitWriter.h"
#include "runtime/core/BitReader.h"

namespace asn1::runtime {

class UperObjectIdentifier {
public:
    static void encode(BitWriter& writer, const ObjectIdentifier& oid);
    static void decode(BitReader& reader, ObjectIdentifier& oid);
};

} // namespace asn1::runtime

#endif // ASN1_RUNTIME_UPER_OBJECT_IDENTIFIER_H