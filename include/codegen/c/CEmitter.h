#ifndef ASN1_CODEGEN_C_EMITTER_H
#define ASN1_CODEGEN_C_EMITTER_H

#include <string>
#include "frontend/AsnNode.h"
#include "frontend/SymbolTable.h"
#include "codegen/Formatter.h"
#include "codegen/TypeMap.h"

namespace asn1::codegen {

// Emits C99 type definitions (typedef struct/enum/union) for each ASN.1
// assignment.  All names are prefixed with the mangled module name to avoid
// collisions in the flat C namespace.
class CEmitter {
public:
    CEmitter();

    void setContext(const frontend::SymbolTable& table,
                   const std::string& module_name);

    std::string emitHeaderPreamble(const std::string& header_guard);
    std::string emitHeaderEpilogue(const std::string& header_guard);

    // Type-definition emitters — each returns the C declaration string.
    std::string emitStruct(const frontend::AsnNodePtr& node,
                           const std::string& module_name);
    std::string emitEnum(const frontend::AsnNodePtr& node,
                         const std::string& module_name);
    std::string emitChoice(const frontend::AsnNodePtr& node,
                           const std::string& module_name);
    std::string emitTypedef(const frontend::AsnNodePtr& node,
                            const std::string& module_name);
    std::string emitSequenceOf(const frontend::AsnNodePtr& node,
                               const std::string& module_name);
    std::string emitValueAssignment(const frontend::AsnNodePtr& node,
                                    const std::string& module_name);

private:
    Formatter formatter;
    int recursion_depth = 0;
    const frontend::SymbolTable* symbol_table = nullptr;
    std::string current_module;

    // Return the C type string for an ASN.1 member node.
    // If the type is an anonymous complex type (inline enum/choice/sequence),
    // the function pre-emits it into `pre_emit` and returns its generated name.
    std::string cTypeFor(const frontend::AsnNodePtr& member_node,
                         const std::string& module_name,
                         const std::string& owner_type_name,
                         const std::string& field_name,
                         std::string& pre_emit);

    // Mangle "ModuleName.TypeName" → "ModuleName_TypeName" for C identifiers.
    static std::string cRef(const std::string& resolved_name,
                            const std::string& current_module);

    // Full C name: "MangedModule_MangledType"
    static std::string cName(const std::string& module_name,
                             const std::string& type_name);
};

} // namespace asn1::codegen

#endif // ASN1_CODEGEN_C_EMITTER_H
