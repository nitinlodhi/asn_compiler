#include "codegen/cpp/CodecEmitter.h"
#include "frontend/AsnNode.h"
#include "utils/Logger.h"
#include "frontend/ConstraintResolver.h"
#include "codegen/TypeMap.h"
#include <algorithm>
#include <vector>
#include <unordered_set>
#include <string>

namespace asn1::codegen {

static inline std::string id(const std::string& name) {
    return TypeMap::mangleName(name);
}

static const std::unordered_set<std::string> kCppKeywords = {
    "true","false","null","nullptr","void","int","long","short","unsigned",
    "signed","char","float","double","bool","auto","class","struct","enum",
    "union","namespace","template","typename","operator","sizeof","alignof",
    "new","delete","return","if","else","for","while","do","switch","case",
    "break","continue","default","static","const","inline","virtual","override",
    "explicit","private","public","protected","friend","using","typedef",
    "noexcept","throw","try","catch","this","final"
};

static inline std::string safeId(const std::string& name) {
    std::string s = id(name);
    if (kCppKeywords.count(s)) s += "_";
    return s;
}

CodecEmitter::CodecEmitter() {}

void CodecEmitter::setContext(const frontend::SymbolTable& table, const std::string& moduleName) {
    this->table = &table;
    this->currentModuleName = moduleName;
}

std::string CodecEmitter::emitEncoderDeclaration(const frontend::AsnNodePtr& assignmentNode, const std::string& moduleName) {
    if (!assignmentNode || assignmentNode->getChildCount() == 0) return "";
    std::string mangledName = id(assignmentNode->name);
    return formatter.formatCode("void encode_" + mangledName + "(asn1::runtime::BitWriter& writer, const " + mangledName + "& value);\n");
}

std::string CodecEmitter::emitDecoderDeclaration(const frontend::AsnNodePtr& assignmentNode, const std::string& moduleName) {
    if (!assignmentNode || assignmentNode->getChildCount() == 0) return "";
    std::string mangledName = id(assignmentNode->name);
    return formatter.formatCode(mangledName + " decode_" + mangledName + "(asn1::runtime::BitReader& reader);\n");
}


std::string CodecEmitter::emitEncoderDefinition(const frontend::AsnNodePtr& assignmentNode, const std::string& moduleName) {
    if (!assignmentNode || assignmentNode->getChildCount() == 0) return "";
    this->recursion_depth = 0; // Reset recursion depth for each top-level type
    this->processingNodes.clear(); // Clear cycle detection set

    auto typeNode = assignmentNode->getChild(0);
    if (!typeNode) return "";
    
    std::string code;
    std::string mangledName = id(assignmentNode->name);
    code += formatter.formatCode("// Encoder for " + mangledName + "\n");
    code += formatter.formatCode("void encode_" + mangledName + "(BitWriter& writer, const " + mangledName + "& value) {\n");
    formatter.indent();

    switch (typeNode->type) {
        case frontend::NodeType::SEQUENCE:
            code += generateSequenceLogic(typeNode, true, "value");
            break;
        case frontend::NodeType::SET:
            code += generateSetLogic(typeNode, true, "value");
            break;
        case frontend::NodeType::SEQUENCE_OF:
            code += generateSequenceOfLogic(typeNode, true, "value");
            break;
        case frontend::NodeType::SET_OF:
            code += generateSetOfLogic(typeNode, true, "value");
            break;
        case frontend::NodeType::CHOICE:
            code += generateChoiceLogic(typeNode, true, "value");
            break;
        case frontend::NodeType::ENUMERATION:
            code += generateEnumeratedLogic(typeNode, mangledName, "value", true);
            break;
        case frontend::NodeType::OCTET_STRING:
            code += generateOctetStringLogic(typeNode, "value", true);
            break;
        case frontend::NodeType::OBJECT_IDENTIFIER:
            code += generateObjectIdentifierLogic(typeNode, "value", true);
            break;
        case frontend::NodeType::UTF8_STRING:
        case frontend::NodeType::PRINTABLE_STRING:
        case frontend::NodeType::VISIBLE_STRING:
        case frontend::NodeType::IA5_STRING:
        case frontend::NodeType::NUMERIC_STRING:
            code += generateCharacterStringLogic(typeNode, "value", true);
            break;
        case frontend::NodeType::ANY_TYPE:
            code += generateAnyLogic(typeNode, "value", true);
            break;
        case frontend::NodeType::NULL_TYPE:
            code += generateNullLogic(typeNode, "value", true);
            break;
        case frontend::NodeType::REAL:
            code += generateRealLogic(typeNode, "value", true);
            break;
        case frontend::NodeType::BIT_STRING:
            code += generateBitStringLogic(typeNode, "value", true);
            break;
        case frontend::NodeType::INTEGER:
            code += generateIntegerLogic(typeNode, "value", true);
            break;
        default:
            code += formatter.formatCode("// TODO: Implement encoding for type " + typeNode->name + "\n");
            break;
    }

    formatter.dedent();
    code += formatter.formatCode("}\n\n");
    
    return code;
}

std::string CodecEmitter::emitDecoderDefinition(const frontend::AsnNodePtr& assignmentNode, const std::string& moduleName) {
    if (!assignmentNode || assignmentNode->getChildCount() == 0) return "";
    this->recursion_depth = 0; // Reset recursion depth for each top-level type
    this->processingNodes.clear(); // Clear cycle detection set

    auto typeNode = assignmentNode->getChild(0);
    if (!typeNode) return "";
    
    std::string code;
    std::string mangledName = id(assignmentNode->name);
    code += formatter.formatCode("// Decoder for " + mangledName + "\n");
    code += formatter.formatCode(mangledName + " decode_" + mangledName + "(BitReader& reader) {\n");
    formatter.indent();
    code += formatter.formatCode(mangledName + " value{};\n");

    switch (typeNode->type) {
        case frontend::NodeType::SEQUENCE:
            code += generateSequenceLogic(typeNode, false, "value");
            break;
        case frontend::NodeType::SET:
            code += generateSetLogic(typeNode, false, "value");
            break;
        case frontend::NodeType::SEQUENCE_OF:
            code += generateSequenceOfLogic(typeNode, false, "value");
            break;
        case frontend::NodeType::SET_OF:
            code += generateSetOfLogic(typeNode, false, "value");
            break;
        case frontend::NodeType::CHOICE:
            code += generateChoiceLogic(typeNode, false, "value");
            break;
        case frontend::NodeType::ENUMERATION:
            code += generateEnumeratedLogic(typeNode, mangledName, "value", false);
            break;
        case frontend::NodeType::OCTET_STRING:
            code += generateOctetStringLogic(typeNode, "value", false);
            break;
        case frontend::NodeType::OBJECT_IDENTIFIER:
            code += generateObjectIdentifierLogic(typeNode, "value", false);
            break;
        case frontend::NodeType::UTF8_STRING:
        case frontend::NodeType::PRINTABLE_STRING:
        case frontend::NodeType::VISIBLE_STRING:
        case frontend::NodeType::IA5_STRING:
        case frontend::NodeType::NUMERIC_STRING:
            code += generateCharacterStringLogic(typeNode, "value", false);
            break;
        case frontend::NodeType::ANY_TYPE:
            code += generateAnyLogic(typeNode, "value", false);
            break;
        case frontend::NodeType::NULL_TYPE:
            code += generateNullLogic(typeNode, "value", false);
            break;
        case frontend::NodeType::REAL:
            code += generateRealLogic(typeNode, "value", false);
            break;
        case frontend::NodeType::BIT_STRING:
            code += generateBitStringLogic(typeNode, "value", false);
            break;
        case frontend::NodeType::INTEGER:
            code += generateIntegerLogic(typeNode, "value", false);
            break;
        default:
            code += formatter.formatCode("// TODO: Implement decoding for type " + typeNode->name + "\n");
            break;
    }

    code += formatter.formatCode("return value;\n");
    formatter.dedent();
    code += formatter.formatCode("}\n\n");
    
    return code;
}

std::string CodecEmitter::generateSetLogic(const frontend::AsnNodePtr& node, bool isEncoder, const std::string& varName) {
    // For UPER, SET is encoded like a SEQUENCE, but the members are first
    // sorted by their canonical tag order. For this implementation, we'll
    // use a stable alphabetical sort of member names as a stand-in for
    // canonical tag sorting.
    
    auto sortedNode = node->deepCopy();
    sortedNode->children.clear(); // We will re-add them in sorted order

    std::string indent(this->recursion_depth * 2, ' ');
    utils::Logger::debug(indent + "-> generateSetLogic for " + node->name + " (" + varName + ")");
    this->recursion_depth++;

    std::vector<frontend::AsnNodePtr> members;
    for (size_t i = 0; i < node->getChildCount(); ++i) {
        members.push_back(node->getChild(i));
    }

    // For SET, sort by canonical tag order.
    std::sort(members.begin(), members.end(), [](const frontend::AsnNodePtr& a, const frontend::AsnNodePtr& b) -> bool {
        // Extension marker should always be last among root components.
        if (a->type == frontend::NodeType::EXTENSION_MARKER) return false;
        if (b->type == frontend::NodeType::EXTENSION_MARKER) return true;

        auto get_tag = [](const frontend::AsnNodePtr& member) -> std::optional<int> {
            if (member->getChild(0) && member->getChild(0)->tag) {
                return member->getChild(0)->tag->tag_number;
            }
            return member->tag_number;
        };
        auto tag_a = get_tag(a);
        auto tag_b = get_tag(b);
        if (!tag_a.has_value() || !tag_b.has_value()) {
            throw std::runtime_error("SET components must have tags for canonical sorting.");
        }
        return tag_a.value() < tag_b.value();
    });

    for (const auto& member : members) {
        sortedNode->addChild(member);
    }

    std::string code = generateSequenceLogic(sortedNode, isEncoder, varName);
    this->recursion_depth--;
    utils::Logger::debug(indent + "<- generateSetLogic for " + node->name);
    return code;
}

std::string CodecEmitter::generateSetOfLogic(const frontend::AsnNodePtr& node, bool isEncoder, const std::string& varName) {
    if (node->getChildCount() == 0) return "";
    auto elementType = node->getChild(0);
    std::string code;

    std::string indent(this->recursion_depth * 2, ' ');
    utils::Logger::debug(indent + "-> generateSetOfLogic for " + node->name + " (" + varName + ")");
    this->recursion_depth++;

    if (isEncoder) {
        code += formatter.formatCode("// For SET OF, values must be sorted before encoding.\n");
        code += formatter.formatCode("// NOTE: This requires operator< to be defined for the element type.\n");
        code += formatter.formatCode("auto sorted_value = " + varName + ";\n");
        code += formatter.formatCode("std::sort(sorted_value.begin(), sorted_value.end());\n");
        
        // Now generate the same logic as SEQUENCE OF, but on the sorted value.
        auto seqOfNode = node->deepCopy();
        seqOfNode->type = frontend::NodeType::SEQUENCE_OF;
        code += generateSequenceOfLogic(seqOfNode, true, "sorted_value");
    } else {
        // Decoder for SET OF is the same as for SEQUENCE OF. No sorting needed.
        code += generateSequenceOfLogic(node, false, varName);
    }

    this->recursion_depth--;
    utils::Logger::debug(indent + "<- generateSetOfLogic for " + node->name);
    return code;
}

std::string CodecEmitter::generateSequenceLogic(const frontend::AsnNodePtr& node, bool isEncoder, const std::string& varName) {
    std::string indent(this->recursion_depth * 2, ' ');
    utils::Logger::debug(indent + "-> generateSequenceLogic for " + node->name + " (" + varName + ")");    
    this->recursion_depth++;
    
    // Safety check: prevent excessive code generation for deeply nested structures
    if (this->recursion_depth > 20) {
        utils::Logger::warning("Recursion depth exceeded for SEQUENCE " + node->name + ". Limiting code generation.");
        this->recursion_depth--;
        return formatter.formatCode("// TODO: Sequence too deeply nested for " + node->name + "\n");
    }
    
    // Track this node to detect cycles
    if (processingNodes.count(node.get())) {
        throw std::runtime_error("Internal error: Circular dependency in generateSequenceLogic for " + node->name);
    }
    processingNodes.insert(node.get());

    auto effectiveNode = node;
    if (node->resolvedTypeNode) {
        effectiveNode = node->resolvedTypeNode;
    }

    // Scoped block so that presenceBitmap and other locals don't conflict with
    // sibling inline SEQUENCE members encoded/decoded at the same depth.
    std::string code = formatter.formatCode("{\n");
    formatter.indent();
    bool hasExtension = effectiveNode->hasExtension;

    // 1. Identify root optional fields
    std::vector<frontend::AsnNodePtr> rootOptionalMembers;
    for (size_t i = 0; i < effectiveNode->getChildCount(); ++i) {
        auto member = effectiveNode->getChild(i);
        if (member->type == frontend::NodeType::EXTENSION_MARKER) {
            break; // Stop at extension marker
        }
        if (member->isOptional || member->hasDefault) {
            rootOptionalMembers.push_back(member);
        }
    }
    const int rootOptionalFieldCount = rootOptionalMembers.size();

    // 2. Generate extension bit logic
    if (hasExtension) {
        if (isEncoder) {
            std::vector<frontend::AsnNodePtr> extensionMembers;
            bool in_extension = false;
            for (size_t i = 0; i < effectiveNode->getChildCount(); ++i) {
                auto member = effectiveNode->getChild(i);
                if (member->type == frontend::NodeType::EXTENSION_MARKER) {
                    in_extension = true;
                    continue;
                }
                if (in_extension) {
                    extensionMembers.push_back(member);
                }
            }

            code += formatter.formatCode("bool has_extensions = false;\n");
            for (const auto& ext_member : extensionMembers) {
                code += formatter.formatCode("if (" + varName + "." + id(ext_member->name) + ".has_value()) { has_extensions = true; }\n");
            }
            code += formatter.formatCode("UperExtension::encodeExtensionMarker(writer, has_extensions);\n\n");
        } else { // Decoder
            code += formatter.formatCode("bool has_extensions = UperExtension::decodeExtensionMarker(reader);\n\n");
        }
    }

    // Helper: build a properly-qualified default-value expression.
    // For identifier defaults (enum values) qualify with std::decay_t<decltype(field)>
    // so enum class values resolve without needing the type name in this scope.
    auto defaultExpr = [&](const frontend::AsnNodePtr& m, const std::string& fieldRef) -> std::string {
        const std::string& raw = m->value.value();
        if (m->defaultValueIsIdentifier)
            return "std::decay_t<decltype(" + fieldRef + ")>::" + id(raw);
        return raw;
    };

    // 3. Generate preamble logic for root members
    if (rootOptionalFieldCount > 0) {
        if (isEncoder) {
            code += formatter.formatCode("uint64_t presenceBitmap = 0;\n");

            for (size_t i = 0; i < rootOptionalMembers.size(); ++i) {
                const auto& member = rootOptionalMembers[i];
                if (member->isOptional) {
                    code += formatter.formatCode("if (" + varName + "." + id(member->name) + ".has_value()) {\n");
                } else { // hasDefault
                    std::string fieldRef = varName + "." + id(member->name);
                    code += formatter.formatCode("if (" + fieldRef + " != " + defaultExpr(member, fieldRef) + ") { // Check against default\n");
                }
                formatter.indent();
                code += formatter.formatCode("presenceBitmap |= (1ULL << " + std::to_string(rootOptionalFieldCount - 1 - i) + ");\n");
                formatter.dedent();
                code += formatter.formatCode("}\n");
            }
            code += formatter.formatCode("UperSequence::encodeSequencePreamble(writer, presenceBitmap, " + std::to_string(rootOptionalFieldCount) + ");\n\n");
        } else { // Decoder
            code += formatter.formatCode("uint64_t presenceBitmap = UperSequence::decodeSequencePreamble(reader, " + std::to_string(rootOptionalFieldCount) + ");\n\n");
        }
    }

    // 4. Generate member encoding/decoding logic for root members
    int optionalIdx = 0;
    for (size_t i = 0; i < effectiveNode->getChildCount(); ++i) {
        auto member = effectiveNode->getChild(i);
        if (member->type == frontend::NodeType::EXTENSION_MARKER) {
            break; // Stop at extension marker
        }
        utils::Logger::debug(indent + "  - Processing member: " + member->name);

        if (member->isOptional || member->hasDefault) {
            if (isEncoder) {
                if (member->isOptional) { // Optional member
                    code += formatter.formatCode("if (" + varName + "." + id(member->name) + ".has_value()) {\n");
                    formatter.indent();
                    code += generateMemberCodecCall(member, "(*" + varName + "." + id(member->name) + ")", true);
                    formatter.dedent();
                    code += formatter.formatCode("}\n");
                } else { // Member with DEFAULT
                    std::string fieldRef = varName + "." + id(member->name);
                    code += formatter.formatCode("if (" + fieldRef + " != " + defaultExpr(member, fieldRef) + ") {\n");
                    formatter.indent();
                    code += generateMemberCodecCall(member, fieldRef, true);
                    formatter.dedent();
                    code += formatter.formatCode("}\n");
                }
            } else { // Decoder
                code += formatter.formatCode("if ((presenceBitmap >> " + std::to_string(rootOptionalFieldCount - 1 - optionalIdx) + ") & 1) {\n");
                formatter.indent();
                if (member->isOptional) {
                    // Derive the inner type from the optional field itself — handles inline
                    // enum/choice/sequence types whose names are local to the struct.
                    std::string fieldRef = varName + "." + id(member->name);
                    std::string tN = "t_" + std::to_string(this->recursion_depth);
                    code += formatter.formatCode("decltype(" + fieldRef + ")::value_type " + tN + "{};\n");
                    code += generateMemberCodecCall(member, tN, false);
                    code += formatter.formatCode(fieldRef + ".emplace(" + tN + ");\n");
                } else { // hasDefault
                    code += generateMemberCodecCall(member, varName + "." + id(member->name), false);
                }
                formatter.dedent();
                code += formatter.formatCode("}\n");
            }
            optionalIdx++;
        } else if (member->getChild(0)->definingFieldName.has_value()) {
            // Special handling for open types (ANY DEFINED BY or parameterized)
            const auto& openTypeNode = member->getChild(0);
            const std::string& defining_field = openTypeNode->definingFieldName.value();
            code += formatter.formatCode("{\n");
            formatter.indent();
            code += formatter.formatCode("BitString open_type_data = UperExtension::decodeOpenType(reader);\n");
            code += formatter.formatCode("switch (" + varName + "." + defining_field + ") {\n");
            formatter.indent();
            for (const auto& pair : openTypeNode->openTypeMap) {
                long long case_id = pair.first;
                const auto& type_val_node = pair.second;
                std::string typeRef = TypeMap::resolvedNameToCppRef(type_val_node->resolvedName.value(), currentModuleName);
                // Build the decode function call (may be namespace-qualified for cross-module).
                auto dotPos = type_val_node->resolvedName.value().find('.');
                std::string decodeFuncName;
                if (dotPos != std::string::npos) {
                    std::string mod = type_val_node->resolvedName.value().substr(0, dotPos);
                    std::string typ = type_val_node->resolvedName.value().substr(dotPos + 1);
                    std::replace(mod.begin(), mod.end(), '-', '_');
                    std::replace(typ.begin(), typ.end(), '-', '_');
                    std::string curMod = currentModuleName;
                    std::replace(curMod.begin(), curMod.end(), '-', '_');
                    decodeFuncName = (mod == curMod) ? ("decode_" + typ) : (mod + "::decode_" + typ);
                } else {
                    decodeFuncName = "decode_" + typeRef;
                }
                code += formatter.formatCode("case " + std::to_string(case_id) + ":\n");
                formatter.indent();
                code += formatter.formatCode(varName + "." + id(member->name) + ".emplace<" + typeRef + ">(" + decodeFuncName + "(BitReader(open_type_data.data.data(), open_type_data.data.size())));\n");
                code += formatter.formatCode("break;\n");
                formatter.dedent();
            }
            code += formatter.formatCode("default: " + varName + "." + id(member->name) + " = open_type_data; break; // Store raw bytes if type is unknown\n");
            formatter.dedent();
            code += formatter.formatCode("}\n");
            formatter.dedent();
            code += formatter.formatCode("}\n");
        } else { // Mandatory field
            code += generateMemberCodecCall(member, varName + "." + id(member->name), isEncoder);
        }
    }

    // 5. Generate extension decoding logic
    if (hasExtension) {
        code += formatter.formatCode("if (has_extensions) {\n");
        formatter.indent();
        if (isEncoder) {
            std::vector<frontend::AsnNodePtr> extensionMembers;
            bool in_extension = false;
            for (size_t i = 0; i < effectiveNode->getChildCount(); ++i) {
                auto member = effectiveNode->getChild(i);
                if (member->type == frontend::NodeType::EXTENSION_MARKER) { in_extension = true; continue; }
                if (in_extension) { extensionMembers.push_back(member); }
            }
            const size_t num_ext_members = extensionMembers.size();

            code += formatter.formatCode("uint64_t extension_bitmap = 0;\n");
            for (size_t i = 0; i < num_ext_members; ++i) {
                code += formatter.formatCode("if (" + varName + "." + id(extensionMembers[i]->name) + ".has_value()) { extension_bitmap |= (1ULL << (" + std::to_string(num_ext_members - 1 - i) + ")); }\n");
            }

            code += formatter.formatCode("UperLength::encodeUnconstrainedLength(writer, " + std::to_string(num_ext_members) + ");\n");
            code += formatter.formatCode("writer.writeBits(extension_bitmap, " + std::to_string(num_ext_members) + ");\n\n");

            for (const auto& ext_member : extensionMembers) {
                code += formatter.formatCode("if (" + varName + "." + id(ext_member->name) + ".has_value()) {\n");
                formatter.indent();
                code += formatter.formatCode("BitWriter open_type_writer;\n");
                code += generateMemberCodecCall(ext_member, "(*" + varName + "." + id(ext_member->name) + ")", true);
                code += formatter.formatCode("BitString open_type_data;\n");
                code += formatter.formatCode("open_type_data.bit_length = open_type_writer.getBitOffset();\n");
                code += formatter.formatCode("open_type_data.data.assign(open_type_writer.getBuffer(), open_type_writer.getBuffer() + open_type_writer.getBufferSize());\n");
                code += formatter.formatCode("UperExtension::encodeOpenType(writer, open_type_data);\n");
                formatter.dedent();
                code += formatter.formatCode("}\n");
            }
            code += formatter.formatCode("// The 'unknown_extensions' field is for round-tripping and is not used during encoding.\n");
        } else { // Decoder
            std::vector<frontend::AsnNodePtr> extensionMembers;
            bool in_extension = false;
            for (size_t i = 0; i < effectiveNode->getChildCount(); ++i) {
                auto member = effectiveNode->getChild(i);
                if (member->type == frontend::NodeType::EXTENSION_MARKER) { in_extension = true; continue; }
                if (in_extension) { extensionMembers.push_back(member); }
            }
            const size_t num_known_exts = extensionMembers.size();

            code += formatter.formatCode("size_t num_extensions_in_message = UperLength::decodeUnconstrainedLength(reader);\n");
            code += formatter.formatCode("uint64_t extension_bitmap = reader.readBits(num_extensions_in_message);\n");
            code += formatter.formatCode("for (size_t i = 0; i < num_extensions_in_message; ++i) {\n");
            formatter.indent();
            code += formatter.formatCode("if ((extension_bitmap >> (num_extensions_in_message - 1 - i)) & 1) {\n");
            formatter.indent();
            if (num_known_exts > 0) {
                code += formatter.formatCode("if (i < " + std::to_string(num_known_exts) + ") {\n");
                formatter.indent();
                code += formatter.formatCode("BitString open_type_data = UperExtension::decodeOpenType(reader);\n");
                code += formatter.formatCode("BitReader open_type_reader(open_type_data.data.data(), open_type_data.data.size());\n");
                code += formatter.formatCode("switch (i) {\n");
                TypeMap typeMap;
                for (size_t j = 0; j < num_known_exts; ++j) {
                    const auto& ext_member = extensionMembers[j];
                    code += formatter.formatCode("    case " + std::to_string(j) + ": {\n");
                    formatter.indent();
                    formatter.indent();
                    std::string extFieldRef = varName + "." + id(ext_member->name);
                    std::string etN = "t_" + std::to_string(this->recursion_depth) + "_" + std::to_string(j);
                    code += formatter.formatCode("decltype(" + extFieldRef + ")::value_type " + etN + "{};\n");
                    std::string member_codec_call = generateMemberCodecCall(ext_member, etN, false);
                    size_t pos = 0;
                    while((pos = member_codec_call.find("reader", pos)) != std::string::npos) {
                        member_codec_call.replace(pos, 6, "open_type_reader");
                        pos += 16;
                    }
                    code += member_codec_call;
                    code += formatter.formatCode(extFieldRef + ".emplace(" + etN + ");\n");
                    code += formatter.formatCode("break;\n");
                    formatter.dedent();
                    formatter.dedent();
                    code += formatter.formatCode("    }\n");
                }
                code += formatter.formatCode("    default:\n");
                code += formatter.formatCode("        // This case should not be hit if all known extensions are handled.\n");
                code += formatter.formatCode("        // As a fallback, store the raw open type.\n");
                code += formatter.formatCode("        " + varName + ".unknown_extensions.push_back(open_type_data);\n");
                code += formatter.formatCode("        break;\n");
                code += formatter.formatCode("}\n");
                formatter.dedent();
                code += formatter.formatCode("} else {\n");
                formatter.indent();
            }
            code += formatter.formatCode(varName + ".unknown_extensions.push_back(UperExtension::decodeOpenType(reader));\n");
            if (num_known_exts > 0) {
                formatter.dedent();
                code += formatter.formatCode("}\n");
            }
            formatter.dedent();
            code += formatter.formatCode("}\n");
            formatter.dedent();
            code += formatter.formatCode("}\n");
        }
        formatter.dedent();
        code += formatter.formatCode("}\n");
    }

    formatter.dedent();
    code += formatter.formatCode("}\n");
    processingNodes.erase(node.get());  // Remove from tracking when done
    this->recursion_depth--;
    utils::Logger::debug(indent + "<- generateSequenceLogic for " + node->name);
    return code;
}

std::string CodecEmitter::generateMemberCodecCall(const frontend::AsnNodePtr& member, const std::string& varName, bool isEncoder) {
    if (this->recursion_depth > 50) { // Add a recursion limit
        throw std::runtime_error("Exceeded maximum recursion depth in CodecEmitter. Possible infinite loop for member: " + member->name);
    }
    std::string indent(this->recursion_depth * 2, ' ');
    utils::Logger::debug(indent + "-> generateMemberCodecCall for: " + member->name + " (" + varName + ")");
    this->recursion_depth++;

    std::string generated_code;
    auto typeNode = member->getChild(0);
    if (!typeNode) return ""; // Should not happen with a valid AST

    frontend::AsnNodePtr effectiveTypeNode = typeNode;
    
    // If the type is a reference, it might have a resolved node pointing to the actual type definition.
    if (typeNode->resolvedTypeNode) {
        effectiveTypeNode = typeNode->resolvedTypeNode;
    }

    // If the original type was a reference to a user-defined type (not a primitive),
    // For user-defined types, we generally want to call their generated functions.
    // HOWEVER, we should NOT try to call a function if:
    // 1. The type is currently being processed (would cause infinite recursion)
    // 2. The type is an inline definition (not a top-level assignment)
    // For now, to fix the immediate issue, we only call generated functions
    // for types that have a module prefix in their qualified name.
    if ((typeNode->type == frontend::NodeType::IDENTIFIER || typeNode->type == frontend::NodeType::FIELD_REFERENCE) &&
        typeNode->resolvedName.has_value() &&
        typeNode->resolvedName.value().find('.') != std::string::npos)
    {
        // Qualified "Module.Type" name → top-level assignment with a generated codec function.
        // Use simple name for same-module, namespace-qualified for cross-module.
        const std::string& resolvedName = typeNode->resolvedName.value();
        auto dot = resolvedName.find('.');
        std::string module = resolvedName.substr(0, dot);
        std::string typeName = resolvedName.substr(dot + 1);
        std::replace(module.begin(), module.end(), '-', '_');
        std::replace(typeName.begin(), typeName.end(), '-', '_');
        std::string curMod = currentModuleName;
        std::replace(curMod.begin(), curMod.end(), '-', '_');

        std::string funcCall;
        if (module == curMod) {
            funcCall = (isEncoder ? "encode_" : "decode_") + typeName;
        } else {
            funcCall = module + "::" + (isEncoder ? "encode_" : "decode_") + typeName;
        }

        if (isEncoder) {
            generated_code = formatter.formatCode(funcCall + "(writer, " + varName + ");\n");
        } else {
            generated_code = formatter.formatCode(varName + " = " + funcCall + "(reader);\n");
        }
    }

    if (generated_code.empty()) {
        // Otherwise, generate code for the effective (possibly primitive) type.
        switch (effectiveTypeNode->type) {
            case frontend::NodeType::SEQUENCE:
                generated_code = generateSequenceLogic(effectiveTypeNode, isEncoder, varName);
                break;
            case frontend::NodeType::SET:
                generated_code = generateSetLogic(effectiveTypeNode, isEncoder, varName);
                break;
            case frontend::NodeType::SEQUENCE_OF:
                generated_code = generateSequenceOfLogic(effectiveTypeNode, isEncoder, varName);
                break;
            case frontend::NodeType::SET_OF:
                generated_code = generateSetOfLogic(effectiveTypeNode, isEncoder, varName);
                break;
            case frontend::NodeType::INTEGER:
                generated_code = generateIntegerLogic(effectiveTypeNode, varName, isEncoder);
                break;
            case frontend::NodeType::BOOLEAN:
                if (isEncoder) {
                    generated_code = formatter.formatCode("writer.writeBits(" + varName + " ? 1 : 0, 1);\n");
                } else {
                    generated_code = formatter.formatCode(varName + " = reader.readBits(1) != 0;\n");
                }
                break;
            case frontend::NodeType::OCTET_STRING:
                generated_code = generateOctetStringLogic(effectiveTypeNode, varName, isEncoder);
                break;
            case frontend::NodeType::BIT_STRING:
                generated_code = generateBitStringLogic(effectiveTypeNode, varName, isEncoder);
                break;
            case frontend::NodeType::OBJECT_IDENTIFIER:
                generated_code = generateObjectIdentifierLogic(effectiveTypeNode, varName, isEncoder);
                break;
            case frontend::NodeType::UTF8_STRING:
            case frontend::NodeType::PRINTABLE_STRING:
            case frontend::NodeType::VISIBLE_STRING:
            case frontend::NodeType::IA5_STRING:
            case frontend::NodeType::NUMERIC_STRING:
                generated_code = generateCharacterStringLogic(effectiveTypeNode, varName, isEncoder);
                break;
            case frontend::NodeType::ANY_TYPE:
                generated_code = generateAnyLogic(effectiveTypeNode, varName, isEncoder);
                break;
            case frontend::NodeType::NULL_TYPE:
                generated_code = generateNullLogic(effectiveTypeNode, varName, isEncoder);
                break;
            case frontend::NodeType::REAL:
                generated_code = generateRealLogic(effectiveTypeNode, varName, isEncoder);
                break;
            case frontend::NodeType::CHOICE:
                generated_code = generateChoiceLogic(effectiveTypeNode, isEncoder, varName);
                break;
            case frontend::NodeType::ENUMERATION:
                generated_code = generateEnumeratedLogic(effectiveTypeNode, "", varName, isEncoder);
                break;
            default:
                if (isEncoder) {
                    generated_code = formatter.formatCode("// TODO: encode " + varName + " of type " + effectiveTypeNode->name + "\n");
                } else {
                    generated_code = formatter.formatCode("// TODO: decode " + varName + " of type " + effectiveTypeNode->name + "\n");
                }
                break;
        }
    }

    this->recursion_depth--;
    utils::Logger::debug(indent + "<- generateMemberCodecCall for: " + member->name);
    return generated_code;
}

std::string CodecEmitter::generateChoiceLogic(const frontend::AsnNodePtr& node, bool isEncoder, const std::string& varName) {
    std::string indent(this->recursion_depth * 2, ' ');
    utils::Logger::debug(indent + "-> generateChoiceLogic for " + node->name + " (" + varName + ")");
    this->recursion_depth++;
    
    // Track this node to detect cycles
    if (processingNodes.count(node.get())) {
        throw std::runtime_error("Internal error: Circular dependency in generateChoiceLogic for " + node->name);
    }
    processingNodes.insert(node.get());

    // Scoped block so that choice_index_ and other locals don't conflict with
    // sibling CHOICE members encoded/decoded at the same recursion depth.
    std::string code = formatter.formatCode("{\n");
    formatter.indent();
    bool hasExtension = node->hasExtension;
    // Unique lambda arg name per nesting level prevents shadowing in nested std::visit.
    std::string argN = "arg_" + std::to_string(this->recursion_depth);

    // 1. Get root choices
    std::vector<frontend::AsnNodePtr> rootChoices;
    for (size_t i = 0; i < node->getChildCount(); ++i) {
        auto choice = node->getChild(i);
        if (choice->type == frontend::NodeType::EXTENSION_MARKER) {
            break;
        }
        rootChoices.push_back(choice);
    }
    const int numRootChoices = rootChoices.size();

    // 2. Handle extension bit
    if (hasExtension) {
        if (isEncoder) {
            code += formatter.formatCode("bool is_extended = std::holds_alternative<asn1::runtime::ExtensionValue>(" + varName + ");\n");
            code += formatter.formatCode("UperExtension::encodeExtensionMarker(writer, is_extended);\n\n");
        } else {
            code += formatter.formatCode("bool is_extended = UperExtension::decodeExtensionMarker(reader);\n\n");
        }
    }

    if (isEncoder) {
        // Encoder logic
        std::string choiceIndexVar = "choice_index_" + std::to_string(this->recursion_depth);
        code += formatter.formatCode("size_t " + choiceIndexVar + " = " + varName + ".index();\n");
        if (hasExtension) {
            code += formatter.formatCode("if (!is_extended) {\n");
            formatter.indent();
            code += formatter.formatCode("UperChoice::encodeChoiceIndex(writer, " + choiceIndexVar + ", " + std::to_string(numRootChoices) + ");\n");
            formatter.dedent();
            code += formatter.formatCode("} else {\n");
            formatter.indent();
            code += formatter.formatCode("const auto& ext_val = std::get<asn1::runtime::ExtensionValue>(" + varName + ");\n");
            code += formatter.formatCode("UperLength::encodeUnconstrainedLength(writer, ext_val.extension_index);\n");
            formatter.dedent();
            code += formatter.formatCode("}\n\n");
        } else {
            code += formatter.formatCode("UperChoice::encodeChoiceIndex(writer, " + choiceIndexVar + ", " + std::to_string(numRootChoices) + ");\n\n");
        }

        code += formatter.formatCode("std::visit([&](auto&& " + argN + ") {\n");
        formatter.indent();
        code += formatter.formatCode("using T = std::decay_t<decltype(" + argN + ")>;\n");

        TypeMap typeMap;
        for (size_t i = 0; i < rootChoices.size(); ++i) {
            const auto& choiceNode = rootChoices[i];
            code += formatter.formatCode("using WrapperType" + std::to_string(i) + " = std::variant_alternative_t<" + std::to_string(i) + ", std::decay_t<decltype(" + varName + ")>>;\n");
            code += formatter.formatCode("if constexpr (std::is_same_v<T, WrapperType" + std::to_string(i) + ">) {\n");
            formatter.indent();
            code += generateMemberCodecCall(choiceNode, argN + "." + safeId(choiceNode->name), true);
            formatter.dedent();
            code += formatter.formatCode("}\n");
        }
        code += formatter.formatCode("if constexpr (std::is_same_v<T, asn1::runtime::ExtensionValue>) {\n");
        formatter.indent();
        code += formatter.formatCode("UperExtension::encodeOpenType(writer, " + argN + ".encoded_value);\n");
        formatter.dedent();
        code += formatter.formatCode("}\n");
        formatter.dedent();
        code += formatter.formatCode("}, " + varName + ");\n");
    } else {
        // Decoder logic
        std::string choiceIndexVar = "choice_index_" + std::to_string(this->recursion_depth);
        code += formatter.formatCode("size_t " + choiceIndexVar + " = 0;\n");
        if (hasExtension) {
            code += formatter.formatCode("if (!is_extended) {\n");
            formatter.indent();
            code += formatter.formatCode(choiceIndexVar + " = UperChoice::decodeChoiceIndex(reader, " + std::to_string(numRootChoices) + ");\n");
            formatter.dedent();
            code += formatter.formatCode("} else {\n");
            formatter.indent();
            code += formatter.formatCode("asn1::runtime::ExtensionValue ext_val;\n");
            code += formatter.formatCode("ext_val.extension_index = UperLength::decodeUnconstrainedLength(reader);\n");
            code += formatter.formatCode("ext_val.encoded_value = UperExtension::decodeOpenType(reader);\n");
            code += formatter.formatCode(varName + ".emplace<asn1::runtime::ExtensionValue>(ext_val);\n");
            formatter.dedent();
            code += formatter.formatCode("}\n\n");
        } else {
            code += formatter.formatCode(choiceIndexVar + " = UperChoice::decodeChoiceIndex(reader, " + std::to_string(numRootChoices) + ");\n\n");
        }

        code += formatter.formatCode("switch (" + choiceIndexVar + ") {\n");
        TypeMap typeMap;
        for (size_t i = 0; i < rootChoices.size(); ++i) {
            const auto& choiceNode = rootChoices[i];

            code += formatter.formatCode("case " + std::to_string(i) + ": {\n");
            formatter.indent();
            {
                std::string itN = "it_" + std::to_string(this->recursion_depth) + "_" + std::to_string(i);
                code += formatter.formatCode("using WrapperType = std::variant_alternative_t<" + std::to_string(i) + ", std::decay_t<decltype(" + varName + ")>>;\n");
                code += formatter.formatCode("WrapperType " + itN + ";\n");
                code += generateMemberCodecCall(choiceNode, itN + "." + safeId(choiceNode->name), false);
                code += formatter.formatCode(varName + ".emplace<WrapperType>(" + itN + ");\n");
            }
            code += formatter.formatCode("break;\n");
            formatter.dedent();
            code += formatter.formatCode("}\n");
        }
        code += formatter.formatCode("default:\n");
        formatter.indent();
        code += formatter.formatCode("throw std::runtime_error(\"Invalid choice index decoded\");\n");
        formatter.dedent();
        code += formatter.formatCode("}\n");
    }

    formatter.dedent();
    code += formatter.formatCode("}\n");
    processingNodes.erase(node.get());  // Remove from tracking when done
    this->recursion_depth--;
    utils::Logger::debug(indent + "<- generateChoiceLogic for " + node->name);
    return code;
}

std::string CodecEmitter::generateEnumeratedLogic(const frontend::AsnNodePtr& node, const std::string& mangledTypeName, const std::string& varName, bool isEncoder) {
    std::string indent(this->recursion_depth * 2, ' ');
    utils::Logger::debug(indent + "-> generateEnumeratedLogic for " + node->name + " (" + varName + ")");
    this->recursion_depth++;

    std::string code;
    bool hasExtension = node->hasExtension;

    // 1. Resolve and sort enumerators to get canonical order
    struct ResolvedEnumerator {
        std::string name;
        int64_t value;
    };
    std::vector<ResolvedEnumerator> resolvedEnumerators;
    int64_t next_value = 0;

    for (size_t i = 0; i < node->getChildCount(); ++i) {
        auto enumeratorNode = node->getChild(i);
        if (enumeratorNode->type == frontend::NodeType::EXTENSION_MARKER) {
            break;
        }
        
        ResolvedEnumerator re;
        re.name = safeId(enumeratorNode->name);
        if (enumeratorNode->value.has_value()) {
            re.value = std::stoll(enumeratorNode->value.value());
        } else {
            re.value = next_value;
        }
        resolvedEnumerators.push_back(re);
        next_value = re.value + 1;
    }

    std::sort(resolvedEnumerators.begin(), resolvedEnumerators.end(), 
              [](const ResolvedEnumerator& a, const ResolvedEnumerator& b) {
        return a.value < b.value;
    });

    const int numRootEnumerators = resolvedEnumerators.size();

    // 2. Generate code
    if (hasExtension) {
        if (isEncoder) {
            code += formatter.formatCode("bool is_extended = true;\n");
            code += formatter.formatCode("switch (" + varName + ") {\n");
            for (const auto& re : resolvedEnumerators) {
                if (!mangledTypeName.empty()) {
                    code += formatter.formatCode("    case " + mangledTypeName + "::" + re.name + ": is_extended = false; break;\n");
                } else {
                    code += formatter.formatCode("    case std::decay_t<decltype(" + varName + ")>::" + re.name + ": is_extended = false; break;\n");
                }
            }
            code += formatter.formatCode("    default: break;\n");
            code += formatter.formatCode("}\n");
            code += formatter.formatCode("UperExtension::encodeExtensionMarker(writer, is_extended);\n\n");
        } else {
            code += formatter.formatCode("bool is_extended = UperExtension::decodeExtensionMarker(reader);\n\n");
        }
    }

    if (isEncoder) {
        code += formatter.formatCode("if (" + std::string(hasExtension ? "!is_extended" : "true") + ") {\n");
        formatter.indent();
        code += formatter.formatCode("uint64_t enum_index = 0;\n");
        code += formatter.formatCode("switch (" + varName + ") {\n");
        for (size_t i = 0; i < resolvedEnumerators.size(); ++i) {
            if (!mangledTypeName.empty()) {
                code += formatter.formatCode("    case " + mangledTypeName + "::" + resolvedEnumerators[i].name + ": enum_index = " + std::to_string(i) + "; break;\n");
            } else {
                code += formatter.formatCode("    case std::decay_t<decltype(" + varName + ")>::" + resolvedEnumerators[i].name + ": enum_index = " + std::to_string(i) + "; break;\n");
            }
        }
        code += formatter.formatCode("    default: throw std::runtime_error(\"Invalid enum value for encoding root\");\n");
        code += formatter.formatCode("}\n");
        code += formatter.formatCode("UperChoice::encodeChoiceIndex(writer, enum_index, " + std::to_string(numRootEnumerators) + ");\n");
        formatter.dedent();
        code += formatter.formatCode("} else {\n");
        formatter.indent();
        code += formatter.formatCode("// Encoding of extended ENUMERATED is not supported.\n");
        code += formatter.formatCode("throw std::runtime_error(\"Encoding of extended ENUMERATED not supported.\");\n");
        formatter.dedent();
        code += formatter.formatCode("}\n");
    } else { // Decoder
        code += formatter.formatCode("if (" + std::string(hasExtension ? "!is_extended" : "true") + ") {\n");
        formatter.indent();
        code += formatter.formatCode("size_t enum_index = UperChoice::decodeChoiceIndex(reader, " + std::to_string(numRootEnumerators) + ");\n");
        code += formatter.formatCode("switch (enum_index) {\n");
        for (size_t i = 0; i < resolvedEnumerators.size(); ++i) {
            if (!mangledTypeName.empty()) {
                code += formatter.formatCode("    case " + std::to_string(i) + ": " + varName + " = " + mangledTypeName + "::" + resolvedEnumerators[i].name + "; break;\n");
            } else {
                code += formatter.formatCode("    case " + std::to_string(i) + ": " + varName + " = std::decay_t<decltype(" + varName + ")>::" + resolvedEnumerators[i].name + "; break;\n");
            }
        }
        code += formatter.formatCode("    default: throw std::runtime_error(\"Invalid enum index decoded\");\n");
        code += formatter.formatCode("}\n");
        formatter.dedent();
        code += formatter.formatCode("} else {\n");
        formatter.indent();
        code += formatter.formatCode("// For ENUMERATED, we can't store the value if it's an unknown extension.\n");
        code += formatter.formatCode("// We decode and discard, then throw to signal an unhandled extension.\n");
        code += formatter.formatCode("UperLength::decodeUnconstrainedLength(reader); // extended_enum_idx\n");
        code += formatter.formatCode("UperExtension::decodeOpenType(reader);\n");
        code += formatter.formatCode("throw std::runtime_error(\"Received an unhandled extended ENUMERATED value.\");\n");
        formatter.dedent();
        code += formatter.formatCode("}\n");
    }

    this->recursion_depth--;
    utils::Logger::debug(indent + "<- generateEnumeratedLogic for " + node->name);
    return code;
}

std::string CodecEmitter::generateOctetStringLogic(const frontend::AsnNodePtr& node, const std::string& varName, bool isEncoder) {
    std::string code;
    bool constrained = false;
    long long minSize = 0, maxSize = 0;

    auto typeInfo = frontend::ConstraintResolver::resolveConstraints(node, *table, currentModuleName);
    if (typeInfo && typeInfo->minValue.has_value() && typeInfo->maxValue.has_value()) {
        minSize = typeInfo->minValue.value();
        maxSize = typeInfo->maxValue.value();
        if (minSize >= 0 && maxSize >= minSize) {
            constrained = true;
        }
    }

    if (isEncoder) {
        if (!constrained) {
            code += formatter.formatCode("UperLength::encodeUnconstrainedLength(writer, " + varName + ".size());\n");
        } else {
            code += formatter.formatCode("UperLength::encodeLength(writer, " + varName + ".size(), " + std::to_string(minSize) + ", " + std::to_string(maxSize) + ");\n");
        }
        code += formatter.formatCode("for (uint8_t byte : " + varName + ") {\n");
        formatter.indent();
        code += formatter.formatCode("writer.writeByte(byte);\n");
        formatter.dedent();
        code += formatter.formatCode("}\n");
    } else { // Decoder
        if (!constrained) {
            code += formatter.formatCode("size_t length = UperLength::decodeUnconstrainedLength(reader);\n");
        } else {
            code += formatter.formatCode("size_t length = UperLength::decodeLength(reader, " + std::to_string(minSize) + ", " + std::to_string(maxSize) + ");\n");
        }
        code += formatter.formatCode(varName + ".resize(length);\n");
        code += formatter.formatCode("for (size_t i = 0; i < length; ++i) {\n");
        formatter.indent();
        code += formatter.formatCode(varName + "[i] = reader.readByte();\n");
        formatter.dedent();
        code += formatter.formatCode("}\n");
    }
    return code;
}

std::string CodecEmitter::generateCharacterStringLogic(const frontend::AsnNodePtr& node, const std::string& varName, bool isEncoder) {
    // For UPER, most character strings are encoded as if they were OCTET STRINGs.
    // The main difference is the permitted alphabet, which is a validation concern.
    // The encoding itself is a length-prefixed sequence of octets.
    std::string code;
    bool constrained = false;
    long long minSize = 0, maxSize = 0;

    auto typeInfo = frontend::ConstraintResolver::resolveConstraints(node, *table, currentModuleName);
    if (typeInfo && typeInfo->minValue.has_value() && typeInfo->maxValue.has_value()) {
        minSize = typeInfo->minValue.value();
        maxSize = typeInfo->maxValue.value();
        if (minSize >= 0 && maxSize >= minSize) {
            constrained = true;
        }
    }

    if (isEncoder) {
        if (!constrained) {
            code += formatter.formatCode("UperLength::encodeUnconstrainedLength(writer, " + varName + ".size());\n");
        } else {
            code += formatter.formatCode("UperLength::encodeLength(writer, " + varName + ".size(), " + std::to_string(minSize) + ", " + std::to_string(maxSize) + ");\n");
        }
        code += formatter.formatCode("writer.writeBytes(reinterpret_cast<const uint8_t*>(" + varName + ".data()), " + varName + ".size() * 8);\n");
    } else { // Decoder
        if (!constrained) {
            code += formatter.formatCode("size_t length = UperLength::decodeUnconstrainedLength(reader);\n");
        } else {
            code += formatter.formatCode("size_t length = UperLength::decodeLength(reader, " + std::to_string(minSize) + ", " + std::to_string(maxSize) + ");\n");
        }
        code += formatter.formatCode(varName + ".resize(length);\n");
        code += formatter.formatCode("reader.readBytes(reinterpret_cast<uint8_t*>(" + varName + ".data()), length * 8);\n");
    }
    return code;
}

std::string CodecEmitter::generateBitStringLogic(const frontend::AsnNodePtr& node, const std::string& varName, bool isEncoder) {
    std::string code;
    bool constrained = false;
    long long minSize = 0, maxSize = 0;

    auto typeInfo = frontend::ConstraintResolver::resolveConstraints(node, *table, currentModuleName);
    if (typeInfo && typeInfo->minValue.has_value() && typeInfo->maxValue.has_value()) {
        minSize = typeInfo->minValue.value();
        maxSize = typeInfo->maxValue.value();
        if (minSize >= 0 && maxSize >= minSize) {
            constrained = true;
        }
    }

    if (isEncoder) {
        if (!constrained) {
            code += formatter.formatCode("UperLength::encodeUnconstrainedLength(writer, " + varName + ".bit_length);\n");
        } else {
            code += formatter.formatCode("UperLength::encodeLength(writer, " + varName + ".bit_length, " + std::to_string(minSize) + ", " + std::to_string(maxSize) + ");\n");
        }
        code += formatter.formatCode("writer.writeBytes(" + varName + ".data.data(), " + varName + ".bit_length);\n");
    } else { // Decoder
        // Scoped block so that byte_length doesn't conflict with sibling BIT STRING members.
        code += formatter.formatCode("{\n");
        formatter.indent();
        if (!constrained) {
            code += formatter.formatCode(varName + ".bit_length = UperLength::decodeUnconstrainedLength(reader);\n");
        } else {
            code += formatter.formatCode(varName + ".bit_length = UperLength::decodeLength(reader, " + std::to_string(minSize) + ", " + std::to_string(maxSize) + ");\n");
        }
        code += formatter.formatCode("size_t byte_length = (" + varName + ".bit_length + 7) / 8;\n");
        code += formatter.formatCode(varName + ".data.resize(byte_length);\n");
        code += formatter.formatCode("if (" + varName + ".bit_length > 0) {\n");
        formatter.indent();
        code += formatter.formatCode("reader.readBytes(" + varName + ".data.data(), " + varName + ".bit_length);\n");
        formatter.dedent();
        code += formatter.formatCode("}\n");
        formatter.dedent();
        code += formatter.formatCode("}\n");
    }
    return code;
}

std::string CodecEmitter::generateObjectIdentifierLogic(const frontend::AsnNodePtr& node, const std::string& varName, bool isEncoder) {
    std::string code;
    if (isEncoder) {
        code += formatter.formatCode("UperObjectIdentifier::encode(writer, " + varName + ");\n");
    } else {
        code += formatter.formatCode("UperObjectIdentifier::decode(reader, " + varName + ");\n");
    }
    return code;
}

std::string CodecEmitter::generateNullLogic(const frontend::AsnNodePtr& node, const std::string& varName, bool isEncoder) {
    // UPER encoding for NULL is a zero-length encoding. Nothing is transmitted.
    return formatter.formatCode("// NULL type has a zero-bit encoding.\n");
}

std::string CodecEmitter::generateAnyLogic(const frontend::AsnNodePtr& node, const std::string& varName, bool isEncoder) {
    // ANY is encoded as an open type. The application is responsible for
    // pre-encoding the data into the BitString before calling the top-level encoder,
    // and for decoding the raw bytes from the BitString after calling the decoder.
    std::string code;
    if (isEncoder) {
        code += formatter.formatCode("UperExtension::encodeOpenType(writer, " + varName + ");\n");
    } else {
        code += formatter.formatCode(varName + " = UperExtension::decodeOpenType(reader);\n");
    }
    return code;
}

std::string CodecEmitter::generateRealLogic(const frontend::AsnNodePtr& node, const std::string& varName, bool isEncoder) {
    std::string code;
    if (isEncoder) {
        code += formatter.formatCode("UperReal::encode(writer, " + varName + ");\n");
    } else {
        code += formatter.formatCode("UperReal::decode(reader, " + varName + ");\n");
    }
    return code;
}

std::string CodecEmitter::generateSequenceOfLogic(const frontend::AsnNodePtr& node, bool isEncoder, const std::string& varName) {
    std::string indent(this->recursion_depth * 2, ' ');
    utils::Logger::debug(indent + "-> generateSequenceOfLogic for " + node->name + " (" + varName + ")");
    this->recursion_depth++;

    if (node->getChildCount() == 0) return "";
    auto elementType = node->getChild(0);
    std::string code;

    // Create a dummy member node for generateMemberCodecCall, which expects an assignment node.
    auto dummyMember = std::make_shared<frontend::AsnNode>(frontend::NodeType::ASSIGNMENT, "element", node->location);
    dummyMember->addChild(elementType);

    bool constrained = false;
    long long minSize = 0, maxSize = 0;

    auto typeInfo = frontend::ConstraintResolver::resolveConstraints(node, *table, currentModuleName);
    if (typeInfo && typeInfo->minValue.has_value() && typeInfo->maxValue.has_value()) {
        minSize = typeInfo->minValue.value();
        maxSize = typeInfo->maxValue.value();
        if (minSize >= 0 && maxSize >= minSize) {
            constrained = true;
        }
    }

    if (isEncoder) {
        if (!constrained) {
            code += formatter.formatCode("UperLength::encodeUnconstrainedLength(writer, " + varName + ".size());\n");
        } else {
            code += formatter.formatCode("UperLength::encodeLength(writer, " + varName + ".size(), " + std::to_string(minSize) + ", " + std::to_string(maxSize) + ");\n");
        }
        code += formatter.formatCode("for (const auto& element : " + varName + ") {\n");
        formatter.indent();
        code += generateMemberCodecCall(dummyMember, "element", true);
        formatter.dedent();
        code += formatter.formatCode("}\n");
    } else { // Decoder
        if (!constrained) {
            code += formatter.formatCode("size_t length = UperLength::decodeUnconstrainedLength(reader);\n");
        } else {
            code += formatter.formatCode("size_t length = UperLength::decodeLength(reader, " + std::to_string(minSize) + ", " + std::to_string(maxSize) + ");\n");
        }
        code += formatter.formatCode(varName + ".resize(length);\n");
        code += formatter.formatCode("for (size_t i = 0; i < length; ++i) {\n");
        formatter.indent();
        code += generateMemberCodecCall(dummyMember, varName + "[i]", false);
        formatter.dedent();
        code += formatter.formatCode("}\n");
    }

    this->recursion_depth--;
    utils::Logger::debug(indent + "<- generateSequenceOfLogic for " + node->name);
    return code;
}

std::string CodecEmitter::generateIntegerLogic(const frontend::AsnNodePtr& node, const std::string& varName, bool isEncoder) {
    auto typeInfo = frontend::ConstraintResolver::resolveConstraints(node, *table, currentModuleName);

    if (typeInfo && typeInfo->minValue.has_value() && typeInfo->maxValue.has_value()) {
        long long minVal = typeInfo->minValue.value();
        long long maxVal = typeInfo->maxValue.value();
        if (maxVal >= minVal) {
            if (isEncoder) {
                return formatter.formatCode("UperInteger::encodeConstrainedInt(writer, " + varName + ", " + std::to_string(minVal) + "LL, " + std::to_string(maxVal) + "LL);\n");
            } else {
                return formatter.formatCode(varName + " = UperInteger::decodeConstrainedInt(reader, " + std::to_string(minVal) + "LL, " + std::to_string(maxVal) + "LL);\n");
            }
        }
    }

    // Default to unconstrained integer
    if (isEncoder) {
        return formatter.formatCode("UperInteger::encodeUnconstrainedInt(writer, " + varName + ");\n");
    } else {
        return formatter.formatCode(varName + " = UperInteger::decodeUnconstrainedInt(reader);\n");
    }
}

} // namespace asn1::codegen
