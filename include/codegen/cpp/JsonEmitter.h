#ifndef ASN1_CODEGEN_CPP_JSON_EMITTER_H
#define ASN1_CODEGEN_CPP_JSON_EMITTER_H

#include <string>
#include <vector>
#include "frontend/AsnNode.h"
#include "codegen/TypeMap.h"

namespace asn1::codegen {

class JsonEmitter {
    TypeMap typeMap;
    int recursion_depth = 0;
    std::string outputNamespace;
    std::string generatedHeader; // basename of the generated .h (e.g. "nr_rrc_15_6_0.h")

    struct RegEntry {
        std::string moduleNs;  // e.g., "NR_RRC_Definitions"
        std::string typeName;  // mangled, e.g., "PhysCellId"
    };
    std::vector<RegEntry> registrations;

    // ── Internal helpers (return code strings) ──────────────────────────────

    // Emit NLOHMANN_JSON_SERIALIZE_ENUM macro for an enum node.
    // qualifiedName: the fully-qualified C++ name for use inside the macro
    //   (e.g. "MyEnum" or "ParentStruct::fieldName_type")
    std::string emitEnumMacro(const frontend::AsnNodePtr& enumNode,
                               const std::string& qualifiedName) const;

    // Emit inline to_json / from_json for a CHOICE type.
    // qualifiedTypeName: the C++ name for the variant alias
    //   (e.g. "MyChoice" or "ParentStruct::fieldName_type")
    // wrapperPrefix: prefix used for wrapper struct names
    //   (same string as the base used when emitting the wrappers, e.g. "MyChoice"
    //    or "ParentStruct::fieldName" – note: NOT the _type suffix)
    std::string emitChoiceAdapterImpl(const frontend::AsnNodePtr& choiceNode,
                                       const std::string& qualifiedTypeName,
                                       const std::string& wrapperPrefix);

    // Emit inline to_json / from_json for a SEQUENCE/SET struct.
    // assignmentNode->name is the simple mangled struct name;
    // structQualifiedName is the name as it appears in the module namespace
    //   (e.g. "MyStruct" for top-level, or "ParentStruct::fieldName_type" for inline).
    // moduleName is needed to resolve cross-module references in field types.
    std::string emitStructAdapterImpl(const frontend::AsnNodePtr& assignmentNode,
                                       const std::string& moduleName,
                                       const std::string& structQualifiedName);

public:
    JsonEmitter();

    void setOutputNamespace(const std::string& ns);
    void setGeneratedHeader(const std::string& header);

    // Emit file-level preamble (#pragma once, includes, runtime adapters).
    std::string emitPreamble();

    // Called for each sorted top-level assignment (dispatch based on type).
    // Returns the adapter code (enum macro, struct to_json/from_json, etc.)
    // to be placed inside the module namespace block.
    std::string emitTypeAdapter(const frontend::AsnNodePtr& assignmentNode,
                                 const std::string& moduleName);

    // Must be called after all emitTypeAdapter calls.
    // Returns the inline registerTypes() function at global scope.
    std::string emitRegisterFunction();
};

} // namespace asn1::codegen

#endif // ASN1_CODEGEN_CPP_JSON_EMITTER_H
