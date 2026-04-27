#ifndef ASN1_FRONTEND_TAG_RESOLVER_H
#define ASN1_FRONTEND_TAG_RESOLVER_H

#include "frontend/AsnNode.h"

namespace asn1::frontend {

/**
 * @brief Traverses the AST to resolve and assign ASN.1 tags.
 * This class applies module-wide tagging rules like AUTOMATIC, IMPLICIT, and EXPLICIT.
 */
class TagResolver {
public:
    void resolve(const AsnNodePtr& module);
};

} // namespace asn1::frontend

#endif // ASN1_FRONTEND_TAG_RESOLVER_H