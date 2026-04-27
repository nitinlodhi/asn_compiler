#ifndef ASN1_CODEGEN_TYPE_MAP_H
#define ASN1_CODEGEN_TYPE_MAP_H

#include <string>
#include <unordered_map>
#include "frontend/AsnTypeInfo.h"

namespace asn1::codegen {

class TypeMap {
private:
    std::unordered_map<std::string, std::string> asnToCppTypes;
    std::unordered_map<std::string, std::string> cppIncludes;

public:
    TypeMap();
    ~TypeMap() = default;

    std::string mapAsnToCppType(const std::string& asnType);
    std::string getCppInclude(const std::string& cppType);
    bool addCustomTypeMapping(const std::string& asnType, const std::string& cppType);
    static std::string mangleName(const std::string& qualifiedName);

    // Converts a qualified "Module.Type" resolved name to a C++ reference.
    // Same module → "Type"; cross-module → "Module::Type".
    static std::string resolvedNameToCppRef(const std::string& resolvedName, const std::string& currentModule);
};

} // namespace asn1::codegen

#endif // ASN1_CODEGEN_TYPE_MAP_H
