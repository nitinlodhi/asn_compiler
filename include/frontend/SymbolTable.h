#ifndef ASN1_FRONTEND_SYMBOL_TABLE_H
#define ASN1_FRONTEND_SYMBOL_TABLE_H

#include <string>
#include <unordered_map>
#include <memory>
#include "frontend/AsnNode.h"
#include "frontend/AsnTypeInfo.h"

namespace asn1::frontend {

class SymbolTable {
private:
    std::unordered_map<std::string, std::unordered_map<std::string, AsnNodePtr>> modules;
    std::unordered_map<std::string, AsnTypeInfoPtr> typeInfo;

public:
    SymbolTable();
    ~SymbolTable() = default;

    bool addSymbol(const std::string& moduleName, const std::string& symbolName, AsnNodePtr node);
    AsnNodePtr lookupSymbol(const std::string& moduleName, const std::string& symbolName) const;
    bool addTypeInfo(const std::string& name, AsnTypeInfoPtr info);
    AsnTypeInfoPtr getTypeInfo(const std::string& name) const;
    
    void resolveReferences(const std::vector<AsnNodePtr>& all_asts);
    bool isSymbolDefined(const std::string& moduleName, const std::string& symbolName) const;
    void printSymbols() const;
};

} // namespace asn1::frontend

#endif // ASN1_FRONTEND_SYMBOL_TABLE_H
