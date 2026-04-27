#ifndef ASN1_CODEGEN_CPP_EMITTER_H
#define ASN1_CODEGEN_CPP_EMITTER_H

#include <string>
#include <vector>
#include <memory>
#include "frontend/AsnNode.h"
#include "codegen/Formatter.h"
#include "codegen/TypeMap.h"

namespace asn1::codegen {

class CppEmitter {
private:
    Formatter formatter;
    TypeMap typeMap;
    int recursion_depth = 0;

public:
    CppEmitter();
    ~CppEmitter() = default;

    std::string emitHeaderPreamble(const std::string& headerGuard);
    std::string emitSourcePreamble(const std::string& headerToInclude);
    std::string emitStruct(const frontend::AsnNodePtr& assignmentNode, const std::string& moduleName);
    std::string emitEnum(const frontend::AsnNodePtr& assignmentNode, const std::string& moduleName);
    std::string emitChoice(const frontend::AsnNodePtr& assignmentNode, const std::string& moduleName);
    std::string emitTypedef(const frontend::AsnNodePtr& assignmentNode, const std::string& moduleName);
    std::string emitValueAssignment(const frontend::AsnNodePtr& assignmentNode, const std::string& moduleName);
    std::string emitSequenceOf(const frontend::AsnNodePtr& assignmentNode, const std::string& moduleName);
    
    void setOutputNamespace(const std::string& ns);
    std::string handleOptionalFields(const frontend::AsnNodePtr& node);
};

} // namespace asn1::codegen

#endif // ASN1_CODEGEN_CPP_EMITTER_H
