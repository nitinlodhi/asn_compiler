#ifndef ASN1_RUNTIME_OBJECT_IDENTIFIER_H
#define ASN1_RUNTIME_OBJECT_IDENTIFIER_H

#include <vector>
#include <cstdint>

namespace asn1::runtime {

using ObjectIdentifier = std::vector<uint64_t>;

} // namespace asn1::runtime

#endif // ASN1_RUNTIME_OBJECT_IDENTIFIER_H