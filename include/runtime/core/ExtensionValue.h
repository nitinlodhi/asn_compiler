#ifndef ASN1_RUNTIME_EXTENSION_VALUE_H
#define ASN1_RUNTIME_EXTENSION_VALUE_H

#include "runtime/core/BitString.h"

namespace asn1::runtime {

struct ExtensionValue {
    size_t extension_index;
    BitString encoded_value;
};

} // namespace asn1::runtime

#endif // ASN1_RUNTIME_EXTENSION_VALUE_H
