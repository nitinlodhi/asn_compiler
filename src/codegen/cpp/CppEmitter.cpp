#include "codegen/cpp/CppEmitter.h"
#include "utils/Logger.h"
#include <algorithm>
#include <cctype>
#include <unordered_set>

namespace asn1::codegen {

// Bare (no '.') resolvedNames are either a known ASN.1 primitive (INTEGER → int64_t,
// etc.) or an unresolved CLASS/IOC template parameter placeholder (TYPE, VALUE …).
// Apply this after TypeMap::resolvedNameToCppRef when resolvedName had no '.' prefix.
static inline void fixBareResolvedName(std::string& typeName, TypeMap& typeMap) {
    // Try primitive mapping first.
    std::string mapped = typeMap.mapAsnToCppType(typeName);
    if (mapped != typeName) {
        typeName = mapped;
        return;
    }
    // Unrecognised bare name: treat as CLASS parameter placeholder → opaque std::any.
    if (typeName.find("::") == std::string::npos && typeName.find('<') == std::string::npos) {
        bool allUpper = !typeName.empty() && std::all_of(typeName.begin(), typeName.end(),
            [](unsigned char c){ return std::isupper(c) || c == '_'; });
        if (allUpper) typeName = "std::any";
    }
}

// Kept for callers that don't need the primitive-mapping path.
static inline void fixClassTemplateParam(std::string& typeName) {
    if (typeName.find("::") == std::string::npos && typeName.find('<') == std::string::npos) {
        bool allUpper = !typeName.empty() && std::all_of(typeName.begin(), typeName.end(),
            [](unsigned char c){ return std::isupper(c) || c == '_'; });
        if (allUpper) typeName = "std::any";
    }
}

// Replace hyphens/dots with underscores so ASN.1 names become valid C++ identifiers.
static inline std::string id(const std::string& name) {
    return TypeMap::mangleName(name);
}

// C++ keywords that can collide with ASN.1 enum/field names.
static bool isCppKeyword(const std::string& s) {
    static const std::unordered_set<std::string> kw = {
        "true","false","null","void","int","long","short","unsigned","signed",
        "char","float","double","class","struct","union","enum","new","delete",
        "this","return","break","continue","for","while","do","if","else",
        "switch","case","default","const","static","auto","register","volatile",
        "inline","template","typename","namespace","using","operator","sizeof",
        "alignof","alignas","try","catch","throw","noexcept","explicit","virtual"
    };
    return kw.count(s) > 0;
}

// Sanitize AND escape C++ keywords for use as identifiers.
static inline std::string safeId(const std::string& name) {
    std::string s = id(name);
    if (isCppKeyword(s)) s += "_";
    return s;
}

CppEmitter::CppEmitter() {}

std::string CppEmitter::emitHeaderPreamble(const std::string& headerGuard) {
    std::string header;
    header += "#ifndef " + headerGuard + "\n";
    header += "#define " + headerGuard + "\n\n";
    header += "#include <cstdint>\n";
    header += "#include <vector>\n";
    header += "#include <optional>\n";
    header += "#include <variant>\n";
    header += "#include <string>\n";
    header += "#include <any>\n";
    header += "#include <cstddef>\n\n"; // For std::nullptr_t
    header += "#include \"runtime/core/BitString.h\"\n";
    header += "#include \"runtime/core/ExtensionValue.h\"\n";
    header += "#include \"runtime/core/ObjectIdentifier.h\"\n";
    header += "#include \"runtime/core/BitWriter.h\"\n";
    header += "#include \"runtime/core/BitReader.h\"\n\n";
    return header;
}

std::string CppEmitter::emitSourcePreamble(const std::string& headerToInclude) {
    std::string preamble;
    size_t last_slash = headerToInclude.find_last_of("/\\");
    std::string include_name = (last_slash == std::string::npos) ? headerToInclude : headerToInclude.substr(last_slash + 1);

    preamble += "#include \"" + include_name + "\"\n\n";
    preamble += "#include <stdexcept>\n";
    preamble += "#include <string>\n\n";
    preamble += "#include \"runtime/uper/UperLength.h\"\n";
    preamble += "#include \"runtime/uper/UperInteger.h\"\n";
    preamble += "#include \"runtime/uper/UperSequence.h\"\n";
    preamble += "#include \"runtime/uper/UperChoice.h\"\n";
    preamble += "#include \"runtime/uper/UperExtension.h\"\n";
    preamble += "#include \"runtime/uper/UperObjectIdentifier.h\"\n";
    preamble += "#include \"runtime/uper/UperReal.h\"\n\n";
    preamble += "using namespace asn1::runtime;\n\n";
    return preamble;
}

std::string CppEmitter::emitStruct(const frontend::AsnNodePtr& assignmentNode, const std::string& moduleName) {
    if (this->recursion_depth > 20) {
        throw std::runtime_error("Exceeded maximum recursion depth in CppEmitter::emitStruct for: " + assignmentNode->name);
    }
    std::string indent(this->recursion_depth * 2, ' ');
    utils::Logger::debug(indent + "-> emitStruct for: " + assignmentNode->name);
    this->recursion_depth++;

    if (!assignmentNode || assignmentNode->getChildCount() == 0) return "";
    auto typeDefNode = assignmentNode->getChild(0);
    if (typeDefNode->resolvedTypeNode) {
        typeDefNode = typeDefNode->resolvedTypeNode;
    }

    if (typeDefNode->type != frontend::NodeType::SEQUENCE && typeDefNode->type != frontend::NodeType::SET) return "";

    std::string code;
    // Simple name only — the module namespace is emitted by CompilerMain.
    std::string mangledName = id(assignmentNode->name);
    code += formatter.formatCode("struct " + mangledName + " {\n");
    formatter.indent();

    // First pass: generate nested types for inline definitions
    for (size_t i = 0; i < typeDefNode->getChildCount(); i++) {
        auto memberNode = typeDefNode->getChild(i);
        if (!memberNode || memberNode->type != frontend::NodeType::ASSIGNMENT) continue;
        auto typeNode = memberNode->getChild(0);

        auto effectiveTypeNode = typeNode;
        if (typeNode && typeNode->resolvedTypeNode) {
            effectiveTypeNode = typeNode->resolvedTypeNode;
        }

        // Only emit inline type definitions for anonymous (unreferenced) types.
        if (typeNode && typeNode->resolvedName.has_value()) continue;

        if (effectiveTypeNode && (effectiveTypeNode->type == frontend::NodeType::SEQUENCE ||
                                   effectiveTypeNode->type == frontend::NodeType::SET)) {
            // Nested struct: name is just memberName_type, defined inside this struct's {}.
            std::string nestedName = id(memberNode->name) + "_type";
            auto dummyAssignment = std::make_shared<frontend::AsnNode>(
                frontend::NodeType::ASSIGNMENT, nestedName, effectiveTypeNode->location);
            dummyAssignment->addChild(effectiveTypeNode);
            code += emitStruct(dummyAssignment, moduleName);
        } else if (effectiveTypeNode && effectiveTypeNode->type == frontend::NodeType::ENUMERATION) {
            std::string nestedEnumName = id(memberNode->name) + "_type";
            code += formatter.formatCode("enum class " + nestedEnumName + " {\n");
            formatter.indent();
            for (size_t j = 0; j < effectiveTypeNode->getChildCount(); j++) {
                auto enumerator = effectiveTypeNode->getChild(j);
                if (!enumerator) continue;
                if (enumerator->type == frontend::NodeType::EXTENSION_MARKER) {
                    code += formatter.formatCode("//... extension marker\n");
                    continue;
                }
                std::string line = safeId(enumerator->name);
                if (enumerator->value.has_value()) {
                    line += " = " + enumerator->value.value();
                }
                line += ",\n";
                code += formatter.formatCode(line);
            }
            formatter.dedent();
            code += formatter.formatCode("};\n");
        } else if (effectiveTypeNode && (effectiveTypeNode->type == frontend::NodeType::SEQUENCE_OF ||
                                          effectiveTypeNode->type == frontend::NodeType::SET_OF)) {
            // If the element type is an inline CHOICE or SEQUENCE, emit a named type for it.
            if (effectiveTypeNode->getChildCount() > 0) {
                auto elemNode = effectiveTypeNode->getChild(0);
                if (!elemNode->resolvedName.has_value()) {
                    if (elemNode->type == frontend::NodeType::CHOICE) {
                        std::string elemTypeName = id(memberNode->name) + "_element_type";
                        auto dummyAssignment = std::make_shared<frontend::AsnNode>(
                            frontend::NodeType::ASSIGNMENT, elemTypeName, elemNode->location);
                        dummyAssignment->addChild(elemNode);
                        code += emitChoice(dummyAssignment, moduleName);
                    } else if (elemNode->type == frontend::NodeType::SEQUENCE ||
                               elemNode->type == frontend::NodeType::SET) {
                        std::string elemTypeName = id(memberNode->name) + "_element_type";
                        auto dummyAssignment = std::make_shared<frontend::AsnNode>(
                            frontend::NodeType::ASSIGNMENT, elemTypeName, elemNode->location);
                        dummyAssignment->addChild(elemNode);
                        code += emitStruct(dummyAssignment, moduleName);
                    }
                }
            }
        } else if (effectiveTypeNode && effectiveTypeNode->type == frontend::NodeType::CHOICE) {
            std::vector<std::string> variant_types;
            std::string baseName = id(memberNode->name);

            // Emit nested types and wrapper structs for each CHOICE alternative.
            for (size_t j = 0; j < effectiveTypeNode->getChildCount(); j++) {
                auto optionNode = effectiveTypeNode->getChild(j);
                if (!optionNode || optionNode->type == frontend::NodeType::EXTENSION_MARKER) continue;
                if (optionNode->type != frontend::NodeType::ASSIGNMENT) continue;

                auto optionTypeNode = optionNode->getChild(0);
                if (!optionTypeNode) continue;

                // Handle inline nested CHOICE or SEQUENCE alternatives.
                if (optionTypeNode->type == frontend::NodeType::CHOICE ||
                    optionTypeNode->type == frontend::NodeType::SEQUENCE) {
                    std::string nestedTypeName = baseName + "_" + id(optionNode->name) + "_type";
                    auto dummyAssignment = std::make_shared<frontend::AsnNode>(
                        frontend::NodeType::ASSIGNMENT, nestedTypeName, optionTypeNode->location);
                    dummyAssignment->addChild(optionTypeNode);
                    if (optionTypeNode->type == frontend::NodeType::SEQUENCE) {
                        code += emitStruct(dummyAssignment, moduleName);
                    } else {
                        code += emitChoice(dummyAssignment, moduleName);
                    }
                } else if (optionTypeNode->type == frontend::NodeType::ENUMERATION &&
                           !optionTypeNode->resolvedName.has_value()) {
                    std::string enumTypeName = baseName + "_" + id(optionNode->name) + "_enum";
                    code += formatter.formatCode("enum class " + enumTypeName + " {\n");
                    formatter.indent();
                    for (size_t k = 0; k < optionTypeNode->getChildCount(); k++) {
                        auto enumerator = optionTypeNode->getChild(k);
                        if (!enumerator) continue;
                        if (enumerator->type == frontend::NodeType::EXTENSION_MARKER) {
                            code += formatter.formatCode("//... extension marker\n");
                            continue;
                        }
                        std::string line = safeId(enumerator->name);
                        if (enumerator->value.has_value()) line += " = " + enumerator->value.value();
                        line += ",\n";
                        code += formatter.formatCode(line);
                    }
                    formatter.dedent();
                    code += formatter.formatCode("};\n");
                }

                // Determine the inner type name for this wrapper struct.
                std::string wrapperStructName = baseName + "_" + id(optionNode->name);
                auto effectiveOptionTypeNode = optionTypeNode;
                if (optionTypeNode->resolvedTypeNode) {
                    effectiveOptionTypeNode = optionTypeNode->resolvedTypeNode;
                }
                std::string innerTypeName;
                if (optionTypeNode->resolvedName.has_value()) {
                    // Named reference: use namespace-qualified form.
                    const auto& rn0 = optionTypeNode->resolvedName.value();
                    innerTypeName = TypeMap::resolvedNameToCppRef(rn0, moduleName);
                    if (rn0.find('.') == std::string::npos) fixBareResolvedName(innerTypeName, typeMap);
                } else if (optionTypeNode->type == frontend::NodeType::CHOICE ||
                           optionTypeNode->type == frontend::NodeType::SEQUENCE) {
                    innerTypeName = baseName + "_" + id(optionNode->name) + "_type";
                } else if (effectiveOptionTypeNode->type == frontend::NodeType::SEQUENCE_OF ||
                           effectiveOptionTypeNode->type == frontend::NodeType::SET_OF) {
                    std::string elemType = "uint8_t";
                    if (effectiveOptionTypeNode->getChildCount() > 0) {
                        auto elemNode = effectiveOptionTypeNode->getChild(0);
                        if (elemNode->resolvedName.has_value()) {
                            const auto& rn1 = elemNode->resolvedName.value();
                            elemType = TypeMap::resolvedNameToCppRef(rn1, moduleName);
                            if (rn1.find('.') == std::string::npos) fixBareResolvedName(elemType, typeMap);
                        } else {
                            std::string et = typeMap.mapAsnToCppType(elemNode->name);
                            if (et != "struct" && et != "enum" && et != "std::vector" &&
                                et != "std::variant" && et != elemNode->name)
                                elemType = et;
                        }
                    }
                    innerTypeName = "std::vector<" + elemType + ">";
                } else if (optionTypeNode->type == frontend::NodeType::ENUMERATION) {
                    innerTypeName = baseName + "_" + id(optionNode->name) + "_enum";
                } else {
                    innerTypeName = typeMap.mapAsnToCppType(optionTypeNode->name);
                    if (innerTypeName == "struct" || innerTypeName == "enum" ||
                        innerTypeName == "std::variant" || innerTypeName == "std::vector" ||
                        innerTypeName == optionTypeNode->name)
                        innerTypeName = "std::any";
                }

                code += formatter.formatCode("struct " + wrapperStructName + " {\n");
                formatter.indent();
                code += formatter.formatCode(innerTypeName + " " + safeId(optionNode->name) + ";\n");
                formatter.dedent();
                code += formatter.formatCode("};\n");

                variant_types.push_back(wrapperStructName);
            }

            // Declare the variant alias for this CHOICE member.
            std::string nestedChoiceName = id(memberNode->name) + "_type";
            code += formatter.formatCode("using " + nestedChoiceName + " = std::variant<\n");
            formatter.indent();

            if (effectiveTypeNode && effectiveTypeNode->hasExtension) {
                variant_types.push_back("asn1::runtime::ExtensionValue");
            }
            for (size_t j = 0; j < variant_types.size(); ++j) {
                code += formatter.formatCode(variant_types[j] + (j < variant_types.size() - 1 ? "," : "") + "\n");
            }
            formatter.dedent();
            code += formatter.formatCode(">;\n\n");
        }
    }

    // Second pass: declare member fields.
    bool in_extension = false;
    for (size_t i = 0; i < typeDefNode->getChildCount(); i++) {
        auto memberNode = typeDefNode->getChild(i);
        if (!memberNode) continue;

        if (memberNode->type == frontend::NodeType::EXTENSION_MARKER) {
            in_extension = true;
            continue;
        }
        if (memberNode->type != frontend::NodeType::ASSIGNMENT) continue;

        auto typeNode = memberNode->getChild(0);
        if (!typeNode) continue;

        std::string typeName;
        auto effectiveTypeNode = typeNode;
        if (typeNode->resolvedTypeNode) {
            effectiveTypeNode = typeNode->resolvedTypeNode;
        }

        if (typeNode->resolvedName.has_value()) {
            // Named reference: module-qualified if cross-module, bare if same-module.
            const auto& rn2 = typeNode->resolvedName.value();
            typeName = TypeMap::resolvedNameToCppRef(rn2, moduleName);
            if (rn2.find('.') == std::string::npos) fixBareResolvedName(typeName, typeMap);
        } else if (effectiveTypeNode && (effectiveTypeNode->type == frontend::NodeType::SEQUENCE ||
                                         effectiveTypeNode->type == frontend::NodeType::SET)) {
            // Inline SEQUENCE/SET: the nested struct is named memberName_type.
            typeName = id(memberNode->name) + "_type";
        } else if (effectiveTypeNode && (effectiveTypeNode->type == frontend::NodeType::SEQUENCE_OF ||
                                         effectiveTypeNode->type == frontend::NodeType::SET_OF)) {
            std::string elemType = "uint8_t";
            if (effectiveTypeNode->getChildCount() > 0) {
                auto elemNode = effectiveTypeNode->getChild(0);
                if (elemNode->resolvedName.has_value()) {
                    const auto& rn3 = elemNode->resolvedName.value();
                    elemType = TypeMap::resolvedNameToCppRef(rn3, moduleName);
                    if (rn3.find('.') == std::string::npos) fixBareResolvedName(elemType, typeMap);
                } else if (elemNode->type == frontend::NodeType::CHOICE ||
                           elemNode->type == frontend::NodeType::SEQUENCE ||
                           elemNode->type == frontend::NodeType::SET) {
                    elemType = id(memberNode->name) + "_element_type";
                } else {
                    std::string et = typeMap.mapAsnToCppType(elemNode->name);
                    if (et != "struct" && et != "enum" && et != "std::vector" &&
                        et != "std::variant" && et != elemNode->name)
                        elemType = et;
                    else
                        elemType = "uint8_t";
                }
            }
            typeName = "std::vector<" + elemType + ">";
        } else if (effectiveTypeNode && effectiveTypeNode->type == frontend::NodeType::ENUMERATION) {
            typeName = id(memberNode->name) + "_type";
        } else if (effectiveTypeNode && effectiveTypeNode->type == frontend::NodeType::CHOICE) {
            typeName = id(memberNode->name) + "_type";
        } else {
            if (typeNode->type == frontend::NodeType::SEQUENCE_OF ||
                typeNode->type == frontend::NodeType::SET_OF) {
                std::string elemType = "uint8_t";
                if (typeNode->getChildCount() > 0) {
                    auto elemNode = typeNode->getChild(0);
                    if (elemNode->resolvedName.has_value()) {
                        const auto& rn4 = elemNode->resolvedName.value();
                        elemType = TypeMap::resolvedNameToCppRef(rn4, moduleName);
                        if (rn4.find('.') == std::string::npos) fixBareResolvedName(elemType, typeMap);
                    } else if (elemNode->type == frontend::NodeType::CHOICE ||
                               elemNode->type == frontend::NodeType::SEQUENCE ||
                               elemNode->type == frontend::NodeType::SET) {
                        elemType = id(memberNode->name) + "_element_type";
                    } else {
                        std::string et = typeMap.mapAsnToCppType(elemNode->name);
                        if (et != "struct" && et != "enum" && et != "std::vector" &&
                            et != "std::variant" && et != elemNode->name)
                            elemType = et;
                        else
                            elemType = "uint8_t";
                    }
                }
                typeName = "std::vector<" + elemType + ">";
            } else {
                typeName = typeMap.mapAsnToCppType(typeNode->name);
                if (typeName == "struct" || typeName == "enum" ||
                    typeName == "std::variant" || typeName == "std::vector" ||
                    typeName == typeNode->name)
                    typeName = "std::any";
            }
        }
        if (memberNode->isOptional || in_extension) {
            typeName = "std::optional<" + typeName + ">";
        }
        std::string member_decl = typeName + " " + id(memberNode->name);
        if (memberNode->hasDefault && memberNode->value.has_value()) {
            const std::string& default_val_str = memberNode->value.value();
            if (memberNode->defaultValueIsIdentifier) {
                std::string default_type_name = typeName;
                if (memberNode->isOptional || in_extension) {
                    default_type_name = default_type_name.substr(14, default_type_name.length() - 15);
                }
                member_decl += " = " + default_type_name + "::" + id(default_val_str);
            } else {
                member_decl += " = " + default_val_str;
            }
        }
        code += formatter.formatCode(member_decl + ";\n");
    }

    if (typeDefNode->hasExtension) {
        code += formatter.formatCode("std::vector<asn1::runtime::BitString> unknown_extensions;\n");
    }

    formatter.dedent();
    code += formatter.formatCode("};\n\n");

    this->recursion_depth--;
    utils::Logger::debug(indent + "<- emitStruct for: " + assignmentNode->name);

    return code;
}

std::string CppEmitter::emitEnum(const frontend::AsnNodePtr& assignmentNode, const std::string& moduleName) {
    if (!assignmentNode || assignmentNode->getChildCount() == 0) return "";
    auto enumNode = assignmentNode->getChild(0);
    if (!enumNode || enumNode->type != frontend::NodeType::ENUMERATION) return "";

    std::string code;
    std::string mangledName = id(assignmentNode->name);
    code += formatter.formatCode("enum class " + mangledName + " {\n");
    formatter.indent();

    for (size_t i = 0; i < enumNode->getChildCount(); i++) {
        auto enumerator = enumNode->getChild(i);
        if (enumerator) {
            if (enumerator->type == frontend::NodeType::EXTENSION_MARKER) {
                code += formatter.formatCode("//... extension marker\n");
                continue;
            }
            std::string line = safeId(enumerator->name);
            if (enumerator->value.has_value()) {
                line += " = " + enumerator->value.value();
            }
            line += ",\n";
            code += formatter.formatCode(line);
        }
    }

    formatter.dedent();
    code += formatter.formatCode("};\n\n");

    return code;
}

std::string CppEmitter::emitChoice(const frontend::AsnNodePtr& assignmentNode, const std::string& moduleName) {
    if (this->recursion_depth > 20) {
        throw std::runtime_error("Exceeded maximum recursion depth in CppEmitter::emitChoice for: " + assignmentNode->name);
    }
    std::string indent(this->recursion_depth * 2, ' ');
    utils::Logger::debug(indent + "-> emitChoice for: " + assignmentNode->name);
    this->recursion_depth++;

    if (!assignmentNode || assignmentNode->getChildCount() == 0) return "";
    auto choiceNode = assignmentNode->getChild(0);
    if (!choiceNode || choiceNode->type != frontend::NodeType::CHOICE) return "";

    std::string code;
    // Simple name — lives inside the module namespace (or inside a parent struct's {}).
    std::string mangledName = id(assignmentNode->name);

    std::vector<std::string> variant_types;
    for (size_t i = 0; i < choiceNode->getChildCount(); i++) {
        auto optionNode = choiceNode->getChild(i);
        if (!optionNode) continue;
        if (optionNode->type == frontend::NodeType::EXTENSION_MARKER) continue;
        if (optionNode->type != frontend::NodeType::ASSIGNMENT) continue;

        auto optionTypeNode = optionNode->getChild(0);
        if (!optionTypeNode) continue;

        // Emit nested type definitions for inline CHOICE/SEQUENCE/ENUM alternatives.
        if (optionTypeNode->type == frontend::NodeType::CHOICE ||
            optionTypeNode->type == frontend::NodeType::SEQUENCE) {
            std::string nestedTypeName = mangledName + "_" + id(optionNode->name) + "_type";
            auto dummyAssignment = std::make_shared<frontend::AsnNode>(
                frontend::NodeType::ASSIGNMENT, nestedTypeName, optionTypeNode->location);
            dummyAssignment->addChild(optionTypeNode);
            if (optionTypeNode->type == frontend::NodeType::SEQUENCE) {
                code += emitStruct(dummyAssignment, moduleName);
            } else {
                code += emitChoice(dummyAssignment, moduleName);
            }
        } else if (optionTypeNode->type == frontend::NodeType::ENUMERATION &&
                   !optionTypeNode->resolvedName.has_value()) {
            std::string enumTypeName = mangledName + "_" + id(optionNode->name) + "_enum";
            code += formatter.formatCode("enum class " + enumTypeName + " {\n");
            formatter.indent();
            for (size_t j = 0; j < optionTypeNode->getChildCount(); j++) {
                auto enumerator = optionTypeNode->getChild(j);
                if (!enumerator) continue;
                if (enumerator->type == frontend::NodeType::EXTENSION_MARKER) {
                    code += formatter.formatCode("//... extension marker\n");
                    continue;
                }
                std::string line = safeId(enumerator->name);
                if (enumerator->value.has_value()) line += " = " + enumerator->value.value();
                line += ",\n";
                code += formatter.formatCode(line);
            }
            formatter.dedent();
            code += formatter.formatCode("};\n");
        }

        // Wrapper struct: named <ChoiceName>_<altName>, holds the alternative value.
        std::string wrapperStructName = mangledName + "_" + id(optionNode->name);
        auto effectiveOptionTypeNode = optionTypeNode;
        if (optionTypeNode->resolvedTypeNode) {
            effectiveOptionTypeNode = optionTypeNode->resolvedTypeNode;
        }

        std::string innerTypeName;
        // Prioritise named references to avoid treating them as inline types.
        if (optionTypeNode->resolvedName.has_value()) {
            const auto& rn5 = optionTypeNode->resolvedName.value();
            innerTypeName = TypeMap::resolvedNameToCppRef(rn5, moduleName);
            if (rn5.find('.') == std::string::npos) fixBareResolvedName(innerTypeName, typeMap);
        } else if (optionTypeNode->type == frontend::NodeType::CHOICE ||
                   optionTypeNode->type == frontend::NodeType::SEQUENCE) {
            // Inline CHOICE/SEQUENCE: the nested type we just emitted.
            innerTypeName = mangledName + "_" + id(optionNode->name) + "_type";
        } else if (effectiveOptionTypeNode->type == frontend::NodeType::SEQUENCE_OF ||
                   effectiveOptionTypeNode->type == frontend::NodeType::SET_OF) {
            std::string elemType = "uint8_t";
            if (effectiveOptionTypeNode->getChildCount() > 0) {
                auto elemNode = effectiveOptionTypeNode->getChild(0);
                if (elemNode->resolvedName.has_value()) {
                    const auto& rn6 = elemNode->resolvedName.value();
                    elemType = TypeMap::resolvedNameToCppRef(rn6, moduleName);
                    if (rn6.find('.') == std::string::npos) fixBareResolvedName(elemType, typeMap);
                } else {
                    std::string et = typeMap.mapAsnToCppType(elemNode->name);
                    if (et != "struct" && et != "enum" && et != "std::vector" &&
                        et != "std::variant" && et != elemNode->name)
                        elemType = et;
                    else
                        elemType = "uint8_t";
                }
            }
            innerTypeName = "std::vector<" + elemType + ">";
        } else if (optionTypeNode->type == frontend::NodeType::ENUMERATION &&
                   !optionTypeNode->resolvedName.has_value()) {
            innerTypeName = mangledName + "_" + id(optionNode->name) + "_enum";
        } else {
            innerTypeName = typeMap.mapAsnToCppType(optionTypeNode->name);
            if (innerTypeName == "struct" || innerTypeName == "enum" ||
                innerTypeName == "std::variant" || innerTypeName == "std::vector" ||
                innerTypeName == optionTypeNode->name)
                innerTypeName = "std::any";
        }

        code += formatter.formatCode("struct " + wrapperStructName + " {\n");
        formatter.indent();
        code += formatter.formatCode(innerTypeName + " " + safeId(optionNode->name) + ";\n");
        formatter.dedent();
        code += formatter.formatCode("};\n");

        variant_types.push_back(wrapperStructName);
    }

    code += formatter.formatCode("using " + mangledName + " = std::variant<\n");
    formatter.indent();

    if (choiceNode->hasExtension) {
        variant_types.push_back("asn1::runtime::ExtensionValue");
    }

    for (size_t i = 0; i < variant_types.size(); ++i) {
        code += formatter.formatCode(variant_types[i] + (i < variant_types.size() - 1 ? "," : "") + "\n");
    }

    formatter.dedent();
    code += formatter.formatCode(">;\n\n");

    this->recursion_depth--;
    utils::Logger::debug(indent + "<- emitChoice for: " + assignmentNode->name);

    return code;
}

std::string CppEmitter::emitTypedef(const frontend::AsnNodePtr& assignmentNode, const std::string& moduleName) {
    if (!assignmentNode || assignmentNode->getChildCount() == 0) return "";
    auto typeNode = assignmentNode->getChild(0);
    if (!typeNode) return "";

    std::string baseType;
    if (typeNode->resolvedName.has_value()) {
        const auto& rn7 = typeNode->resolvedName.value();
        baseType = TypeMap::resolvedNameToCppRef(rn7, moduleName);
        if (rn7.find('.') == std::string::npos) fixBareResolvedName(baseType, typeMap);
    } else {
        baseType = id(typeMap.mapAsnToCppType(typeNode->name));
    }
    if (baseType.empty() || baseType == "struct" || baseType == "enum"
        || baseType == "std::variant" || baseType == "std::vector"
        || baseType == "std::any")
        return "";
    std::string mangledName = id(assignmentNode->name);
    return formatter.formatCode("using " + mangledName + " = " + baseType + ";\n\n");
}

std::string CppEmitter::emitSequenceOf(const frontend::AsnNodePtr& assignmentNode, const std::string& moduleName) {
    if (!assignmentNode || assignmentNode->getChildCount() == 0) return "";
    auto seqOfNode = assignmentNode->getChild(0);
    if (!seqOfNode || seqOfNode->type != frontend::NodeType::SEQUENCE_OF || seqOfNode->getChildCount() == 0) {
        return "";
    }

    auto elementTypeNode = seqOfNode->getChild(0);
    std::string mangledName = id(assignmentNode->name);
    std::string code;
    std::string elementTypeName;

    if (elementTypeNode->resolvedName.has_value()) {
        const auto& rn8 = elementTypeNode->resolvedName.value();
        elementTypeName = TypeMap::resolvedNameToCppRef(rn8, moduleName);
        if (rn8.find('.') == std::string::npos) fixBareResolvedName(elementTypeName, typeMap);
    } else if (elementTypeNode->type == frontend::NodeType::CHOICE) {
        std::string elemTypeName = mangledName + "_element";
        auto dummyAssignment = std::make_shared<frontend::AsnNode>(
            frontend::NodeType::ASSIGNMENT, elemTypeName, elementTypeNode->location);
        dummyAssignment->addChild(elementTypeNode);
        code += emitChoice(dummyAssignment, moduleName);
        elementTypeName = elemTypeName;
    } else if (elementTypeNode->type == frontend::NodeType::SEQUENCE) {
        std::string elemTypeName = mangledName + "_element";
        auto dummyAssignment = std::make_shared<frontend::AsnNode>(
            frontend::NodeType::ASSIGNMENT, elemTypeName, elementTypeNode->location);
        dummyAssignment->addChild(elementTypeNode);
        code += emitStruct(dummyAssignment, moduleName);
        elementTypeName = elemTypeName;
    } else {
        elementTypeName = typeMap.mapAsnToCppType(elementTypeNode->name);
        if (elementTypeName == "struct" || elementTypeName == "enum" ||
            elementTypeName == "std::variant" || elementTypeName == "std::vector")
            elementTypeName = "uint8_t";
        else if (elementTypeName == elementTypeNode->name)
            elementTypeName = id(elementTypeName); // mangle user-defined type name (e.g. Foo-Bar → Foo_Bar)
    }
    code += formatter.formatCode("using " + mangledName + " = std::vector<" + elementTypeName + ">;\n\n");
    return code;
}

std::string CppEmitter::emitValueAssignment(const frontend::AsnNodePtr& assignmentNode, const std::string& moduleName) {
    if (!assignmentNode || assignmentNode->getChildCount() < 2) return "";
    auto typeNode = assignmentNode->getChild(0);
    auto valueNode = assignmentNode->getChild(1);

    std::string cppType;
    if (typeNode->resolvedName.has_value()) {
        const auto& rn9 = typeNode->resolvedName.value();
        cppType = TypeMap::resolvedNameToCppRef(rn9, moduleName);
        if (rn9.find('.') == std::string::npos) fixBareResolvedName(cppType, typeMap);
    } else {
        cppType = typeMap.mapAsnToCppType(typeNode->name);
    }

    std::string varName = safeId(assignmentNode->name);
    std::string valueStr = valueNode->value.value_or("");

    if (typeNode->type == frontend::NodeType::INTEGER) {
        return formatter.formatCode("constexpr " + cppType + " " + varName + " = " + valueStr + ";\n\n");
    } else if (typeNode->type == frontend::NodeType::OBJECT_IDENTIFIER) {
        std::string oid_values = valueStr;
        std::replace(oid_values.begin(), oid_values.end(), ' ', ',');
        return formatter.formatCode("const " + cppType + " " + varName + " = { " + oid_values + " };\n\n");
    }

    return "// Value assignment for " + varName + " of type " + typeNode->name + " not implemented\n";
}

void CppEmitter::setOutputNamespace(const std::string& ns) {
    (void)ns;
}

std::string CppEmitter::handleOptionalFields(const frontend::AsnNodePtr& node) {
    (void)node;
    return "";
}

} // namespace asn1::codegen
