#ifndef ASN1_CODEGEN_C_CODEC_EMITTER_H
#define ASN1_CODEGEN_C_CODEC_EMITTER_H

#include <set>
#include <string>
#include "frontend/AsnNode.h"
#include "frontend/SymbolTable.h"
#include "codegen/Formatter.h"
#include "codegen/TypeMap.h"
#include "frontend/ConstraintResolver.h"

namespace asn1::codegen {

// Emits C99 encode_*/decode_* function declarations and definitions for each
// ASN.1 type.  Functions return int (0 = success, -1 = constraint violation).
// Error details are written to a caller-supplied err_buf.
class CCodecEmitter {
public:
    CCodecEmitter();

    void setContext(const frontend::SymbolTable& table,
                   const std::string& module_name);

    // Declaration (goes in the .h file)
    std::string emitEncoderDeclaration(const frontend::AsnNodePtr& node,
                                       const std::string& module_name);
    std::string emitDecoderDeclaration(const frontend::AsnNodePtr& node,
                                       const std::string& module_name);

    // Definition (goes in the .c file)
    std::string emitEncoderDefinition(const frontend::AsnNodePtr& node,
                                      const std::string& module_name);
    std::string emitDecoderDefinition(const frontend::AsnNodePtr& node,
                                      const std::string& module_name);

private:
    Formatter formatter;
    const frontend::SymbolTable* symbol_table = nullptr;
    std::string current_module;

    // Accumulated static helper codec functions for parameterized-type instantiations.
    // generateSequenceLogic/generateChoiceLogic append to these; emitEncoder/DecoderDefinition
    // drains them and prepends to the emitted function.
    std::string pending_helpers_enc_;
    std::string pending_helpers_dec_;

    // Per-type codec body generators (encoder + decoder combined).
    std::string generateBitStringLogic(const frontend::AsnNodePtr& type_node,
                                       const std::string& var, bool is_enc,
                                       const std::string& prefix);
    std::string generateOctetStringLogic(const frontend::AsnNodePtr& type_node,
                                         const std::string& var, bool is_enc,
                                         const std::string& prefix);
    std::string generateIntegerLogic(const frontend::AsnNodePtr& type_node,
                                     const std::string& var, bool is_enc,
                                     const std::string& prefix);
    std::string generateBooleanLogic(const std::string& var, bool is_enc);
    std::string generateEnumeratedLogic(const frontend::AsnNodePtr& type_node,
                                        const std::string& var, bool is_enc);
    std::string generateSequenceLogic(const frontend::AsnNodePtr& node,
                                      const std::string& module_name,
                                      const std::string& prefix,
                                      bool is_enc);
    std::string generateChoiceLogic(const frontend::AsnNodePtr& node,
                                    const std::string& module_name,
                                    const std::string& prefix,
                                    bool is_enc);
    std::string generateSequenceOfLogic(const frontend::AsnNodePtr& node,
                                        const std::string& module_name,
                                        const std::string& prefix,
                                        bool is_enc);
    std::string generateCharStringLogic(const frontend::AsnNodePtr& type_node,
                                        const std::string& var, bool is_enc,
                                        const std::string& prefix);

    // Resolve the effective type node for a member, following aliases.
    frontend::AsnNodePtr resolveEffectiveType(const frontend::AsnNodePtr& node);

    // Build the encode/decode call for a named type (used from SEQUENCE/CHOICE).
    std::string encodeCallFor(const std::string& type_prefix,
                              const std::string& type_name,
                              const std::string& expr,
                              bool is_scalar);
    std::string decodeCallFor(const std::string& type_prefix,
                              const std::string& type_name,
                              const std::string& out_expr,
                              bool is_scalar);

    static std::string cName(const std::string& module_name,
                             const std::string& type_name);
    static std::string cRef(const std::string& resolved_name,
                            const std::string& current_module);
    static bool isScalarCType(const frontend::AsnNodePtr& type_node);

    // Internal: generates the function body without draining pending_helpers.
    // The public emitEncoder/DecoderDefinition wraps this to collect helpers.
    std::string emitEncoderDefinitionRaw(const frontend::AsnNodePtr& node,
                                         const std::string& module_name);
    std::string emitDecoderDefinitionRaw(const frontend::AsnNodePtr& node,
                                         const std::string& module_name);
};

} // namespace asn1::codegen

#endif // ASN1_CODEGEN_C_CODEC_EMITTER_H
