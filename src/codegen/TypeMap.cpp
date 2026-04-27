#include "codegen/TypeMap.h"
#include <algorithm>

namespace asn1::codegen {

TypeMap::TypeMap() {
    // Initialize ASN.1 to C++ type mappings
    asnToCppTypes["INTEGER"] = "int64_t";
    asnToCppTypes["BOOLEAN"] = "bool";
    asnToCppTypes["OCTET STRING"] = "std::vector<uint8_t>";
    asnToCppTypes["BIT STRING"] = "asn1::runtime::BitString";
    asnToCppTypes["ENUMERATED"] = "enum";
    asnToCppTypes["OBJECT IDENTIFIER"] = "asn1::runtime::ObjectIdentifier";
    asnToCppTypes["ANY"] = "std::any";
    asnToCppTypes["UTF8String"] = "std::string";
    asnToCppTypes["PrintableString"] = "std::string";
    asnToCppTypes["VisibleString"] = "std::string";
    asnToCppTypes["IA5String"] = "std::string";
    asnToCppTypes["NumericString"] = "std::string";
    asnToCppTypes["NULL"] = "std::nullptr_t";
    asnToCppTypes["REAL"] = "double";
    asnToCppTypes["SEQUENCE"] = "struct";
    asnToCppTypes["CHOICE"] = "std::variant";
    asnToCppTypes["SEQUENCE OF"] = "std::vector";
}

std::string TypeMap::mapAsnToCppType(const std::string& asnType) {
    auto it = asnToCppTypes.find(asnType);
    if (it != asnToCppTypes.end()) {
        return it->second;
    }
    return asnType; // Return as-is if not found
}

std::string TypeMap::getCppInclude(const std::string& cppType) {
    if (cppType.find("std::vector") != std::string::npos) {
        return "#include <vector>";
    }
    if (cppType.find("std::variant") != std::string::npos) {
        return "#include <variant>";
    }
    if (cppType.find("std::optional") != std::string::npos) {
        return "#include <optional>";
    }
    return "";
}

bool TypeMap::addCustomTypeMapping(const std::string& asnType, const std::string& cppType) {
    asnToCppTypes[asnType] = cppType;
    return true;
}

std::string TypeMap::mangleName(const std::string& qualifiedName) {
    std::string mangled = qualifiedName;
    std::replace(mangled.begin(), mangled.end(), '.', '_');
    std::replace(mangled.begin(), mangled.end(), '-', '_');
    return mangled;
}

std::string TypeMap::resolvedNameToCppRef(const std::string& resolvedName, const std::string& currentModule) {
    auto dot = resolvedName.find('.');
    if (dot == std::string::npos) {
        std::string t = resolvedName;
        std::replace(t.begin(), t.end(), '-', '_');
        return t;
    }
    std::string module = resolvedName.substr(0, dot);
    std::string typeName = resolvedName.substr(dot + 1);
    std::replace(module.begin(), module.end(), '-', '_');
    std::replace(typeName.begin(), typeName.end(), '-', '_');
    std::string curMod = currentModule;
    std::replace(curMod.begin(), curMod.end(), '-', '_');
    if (module == curMod)
        return typeName;
    return module + "::" + typeName;
}

} // namespace asn1::codegen
