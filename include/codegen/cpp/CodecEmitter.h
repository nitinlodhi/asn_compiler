#ifndef ASN1_CODEGEN_CODEC_EMITTER_H
#define ASN1_CODEGEN_CODEC_EMITTER_H

#include <string>
#include <memory>
#include <set>
#include "frontend/AsnNode.h"
#include "codegen/TemplateManager.h"
#include "codegen/Formatter.h"
#include "codegen/TypeMap.h"

namespace asn1::frontend {
class SymbolTable;
}

namespace asn1::codegen {

class CodecEmitter {
private:
    TemplateManager templateManager;
    Formatter formatter;
    TypeMap typeMap;
    const frontend::SymbolTable* table = nullptr;
    std::string currentModuleName;
    int recursion_depth = 0;
    std::set<const void*> processingNodes;  // Track node pointers being processed to detect cycles
    std::string generateMemberCodecCall(const frontend::AsnNodePtr& member, const std::string& varName, bool isEncoder);

public:
    CodecEmitter();
    ~CodecEmitter() = default;

    void setContext(const frontend::SymbolTable& table, const std::string& moduleName);

    std::string emitEncoderDeclaration(const frontend::AsnNodePtr& assignmentNode, const std::string& moduleName);
    std::string emitDecoderDeclaration(const frontend::AsnNodePtr& assignmentNode, const std::string& moduleName);
    std::string emitEncoderDefinition(const frontend::AsnNodePtr& assignmentNode, const std::string& moduleName);
    std::string emitDecoderDefinition(const frontend::AsnNodePtr& assignmentNode, const std::string& moduleName);
    std::string generateSequenceLogic(const frontend::AsnNodePtr& node, bool isEncoder, const std::string& varName = "value");
    std::string generateChoiceLogic(const frontend::AsnNodePtr& node, bool isEncoder, const std::string& varName = "value");
    std::string generateSetLogic(const frontend::AsnNodePtr& node, bool isEncoder, const std::string& varName = "value");
    std::string generateSetOfLogic(const frontend::AsnNodePtr& node, bool isEncoder, const std::string& varName = "value");
    std::string generateSequenceOfLogic(const frontend::AsnNodePtr& node, bool isEncoder, const std::string& varName = "value");
    std::string generateEnumeratedLogic(const frontend::AsnNodePtr& node, const std::string& mangledTypeName, const std::string& varName, bool isEncoder);
    std::string generateOctetStringLogic(const frontend::AsnNodePtr& node, const std::string& varName, bool isEncoder);
    std::string generateObjectIdentifierLogic(const frontend::AsnNodePtr& node, const std::string& varName, bool isEncoder);
    std::string generateCharacterStringLogic(const frontend::AsnNodePtr& node, const std::string& varName, bool isEncoder);
    std::string generateAnyLogic(const frontend::AsnNodePtr& node, const std::string& varName, bool isEncoder);
    std::string generateNullLogic(const frontend::AsnNodePtr& node, const std::string& varName, bool isEncoder);
    std::string generateRealLogic(const frontend::AsnNodePtr& node, const std::string& varName, bool isEncoder);
    std::string generateBitStringLogic(const frontend::AsnNodePtr& node, const std::string& varName, bool isEncoder);
    std::string generateIntegerLogic(const frontend::AsnNodePtr& node, const std::string& varName, bool isEncoder);
};

} // namespace asn1::codegen

#endif // ASN1_CODEGEN_CODEC_EMITTER_H