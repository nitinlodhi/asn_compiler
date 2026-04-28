#ifndef ASN1_FRONTEND_CONSTRAINT_RESOLVER_H
#define ASN1_FRONTEND_CONSTRAINT_RESOLVER_H

#include <string>
#include <memory>
#include <optional>
#include "frontend/AsnNode.h"
#include "frontend/AsnTypeInfo.h"

namespace asn1::frontend {

class SymbolTable; // Forward declaration

class ConstraintResolver {
public:
    static AsnTypeInfoPtr resolveConstraints(const AsnNodePtr& typeNode, const SymbolTable& table, const std::string& moduleName);
private:
    static std::optional<long long> resolveBound(const AsnNodePtr& boundNode, const SymbolTable& table, const std::string& moduleName);
};

} // namespace asn1::frontend

#endif // ASN1_FRONTEND_CONSTRAINT_RESOLVER_H
