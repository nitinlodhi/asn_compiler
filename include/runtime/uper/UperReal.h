#ifndef ASN1_RUNTIME_UPER_REAL_H
#define ASN1_RUNTIME_UPER_REAL_H

#include "runtime/core/BitWriter.h"
#include "runtime/core/BitReader.h"

namespace asn1::runtime {

class UperReal {
public:
    static void encode(BitWriter& writer, double value);
    static void decode(BitReader& reader, double& value);
};

} // namespace asn1::runtime

#endif // ASN1_RUNTIME_UPER_REAL_H