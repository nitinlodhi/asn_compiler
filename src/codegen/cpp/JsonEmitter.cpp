#include "codegen/cpp/JsonEmitter.h"
#include "utils/Logger.h"
#include <algorithm>
#include <unordered_set>

namespace asn1::codegen {

// ---------------------------------------------------------------------------
// Local helpers (module-private)
// ---------------------------------------------------------------------------

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

// ---------------------------------------------------------------------------
// JsonEmitter
// ---------------------------------------------------------------------------

JsonEmitter::JsonEmitter() {}

void JsonEmitter::setOutputNamespace(const std::string& ns) {
    outputNamespace = ns;
}

void JsonEmitter::setGeneratedHeader(const std::string& header) {
    generatedHeader = header;
}

// ---------------------------------------------------------------------------
// emitPreamble
// ---------------------------------------------------------------------------

std::string JsonEmitter::emitPreamble() {
    std::string code;
    code += "#pragma once\n";
    code += "#include \"" + generatedHeader + "\"\n";
    code += "#include <nlohmann/json.hpp>\n";
    code += "#include <functional>\n";
    code += "#include <unordered_map>\n";
    code += "#include <sstream>\n";
    code += "#include <iomanip>\n";
    code += "#include <stdexcept>\n";
    code += "\n";

    // Runtime adapters for BitString and ExtensionValue
    code += "namespace asn1::runtime {\n\n";

    // BitString: {"hex": "deadbeef", "bit_length": 15}
    code += "inline void to_json(nlohmann::json& j, const asn1::runtime::BitString& v) {\n";
    code += "    std::ostringstream oss;\n";
    code += "    oss << std::hex << std::setfill('0');\n";
    code += "    for (uint8_t byte : v.data) oss << std::setw(2) << static_cast<int>(byte);\n";
    code += "    j = nlohmann::json{{\"hex\", oss.str()}, {\"bit_length\", v.bit_length}};\n";
    code += "}\n\n";

    code += "inline void from_json(const nlohmann::json& j, asn1::runtime::BitString& v) {\n";
    code += "    v.bit_length = j.at(\"bit_length\").get<size_t>();\n";
    code += "    std::string hex = j.at(\"hex\").get<std::string>();\n";
    code += "    v.data.clear();\n";
    code += "    for (size_t i = 0; i + 1 < hex.size(); i += 2) {\n";
    code += "        v.data.push_back(static_cast<uint8_t>(std::stoul(hex.substr(i, 2), nullptr, 16)));\n";
    code += "    }\n";
    code += "}\n\n";

    // ExtensionValue: to_json → {"__extension": true}; from_json → throws
    code += "inline void to_json(nlohmann::json& j, const asn1::runtime::ExtensionValue& /*v*/) {\n";
    code += "    j = nlohmann::json{{\"__extension\", true}};\n";
    code += "}\n\n";

    code += "inline void from_json(const nlohmann::json& /*j*/, asn1::runtime::ExtensionValue& /*v*/) {\n";
    code += "    throw std::runtime_error(\"Cannot deserialize ExtensionValue from JSON\");\n";
    code += "}\n\n";

    code += "} // namespace asn1::runtime\n\n";

    // std::any adapter: opaque fields serialize as null, deserialize as no-op.
    code += "namespace nlohmann {\n";
    code += "template <> struct adl_serializer<std::any> {\n";
    code += "    static void to_json(json& j, const std::any& /*v*/) { j = nullptr; }\n";
    code += "    static void from_json(const json& /*j*/, std::any& /*v*/) {}\n";
    code += "};\n";
    code += "} // namespace nlohmann\n\n";

    return code;
}

// ---------------------------------------------------------------------------
// emitEnumMacro
// ---------------------------------------------------------------------------

std::string JsonEmitter::emitEnumMacro(const frontend::AsnNodePtr& enumNode,
                                        const std::string& qualifiedName) const {
    // Collect enumerators (skip EXTENSION_MARKER).
    std::string code;
    code += "NLOHMANN_JSON_SERIALIZE_ENUM(" + qualifiedName + ", {\n";

    bool first = true;
    for (size_t i = 0; i < enumNode->getChildCount(); ++i) {
        auto enumerator = enumNode->getChild(i);
        if (!enumerator || enumerator->type == frontend::NodeType::EXTENSION_MARKER) continue;
        if (!first) code += ",\n";
        first = false;
        std::string valName = safeId(enumerator->name);
        code += "    {" + qualifiedName + "::" + valName + ", \"" + valName + "\"}";
    }
    code += "\n})\n\n";
    return code;
}

// ---------------------------------------------------------------------------
// emitChoiceAdapterImpl
// ---------------------------------------------------------------------------

std::string JsonEmitter::emitChoiceAdapterImpl(const frontend::AsnNodePtr& choiceNode,
                                                const std::string& qualifiedTypeName,
                                                const std::string& wrapperPrefix) const {
    // Collect root alternatives (skip EXTENSION_MARKER).
    struct Alt {
        std::string altName;       // e.g., "nr_SCG" (mangled)
        std::string wrapperType;   // fully-qualified wrapper struct name
        bool isNull = false;       // true if the inner type is std::nullptr_t
    };
    std::vector<Alt> alts;

    for (size_t i = 0; i < choiceNode->getChildCount(); ++i) {
        auto optionNode = choiceNode->getChild(i);
        if (!optionNode || optionNode->type == frontend::NodeType::EXTENSION_MARKER) continue;
        if (optionNode->type != frontend::NodeType::ASSIGNMENT) continue;

        auto optionTypeNode = optionNode->getChild(0);
        if (!optionTypeNode) continue;

        Alt a;
        a.altName = id(optionNode->name);

        // Wrapper struct name mirrors what CppEmitter produced:
        // wrapperPrefix + "_" + altName
        // wrapperPrefix is already the qualified form (e.g. "ParentStruct::fieldName"
        // or just "MyChoice").  Inside the same namespace scope, we don't need to
        // fully qualify further – the callers always emit us inside a namespace block.
        a.wrapperType = wrapperPrefix + "_" + a.altName;

        // Detect NULL_TYPE inner field
        auto effectiveTypeNode = optionTypeNode;
        if (optionTypeNode->resolvedTypeNode) effectiveTypeNode = optionTypeNode->resolvedTypeNode;
        if (!optionTypeNode->resolvedName.has_value() &&
            effectiveTypeNode->type == frontend::NodeType::NULL_TYPE)
            a.isNull = true;

        alts.push_back(std::move(a));
    }

    bool hasExtension = choiceNode->hasExtension;

    std::string code;

    // ── to_json ──────────────────────────────────────────────────────────────
    // Format: {"altName": value}  (single-key object, key = alternative name)
    code += "inline void to_json(nlohmann::json& j, const " + qualifiedTypeName + "& v) {\n";
    code += "    std::visit([&j](const auto& alt) {\n";
    code += "        using T = std::decay_t<decltype(alt)>;\n";
    for (size_t i = 0; i < alts.size(); ++i) {
        const auto& a = alts[i];
        code += "        if constexpr (std::is_same_v<T, " + a.wrapperType + ">) {\n";
        if (a.isNull) {
            code += "            j = nlohmann::json{{\"" + a.altName + "\", nullptr}};\n";
        } else {
            code += "            j = nlohmann::json{{\"" + a.altName + "\", alt." + safeId(a.altName) + "}};\n";
        }
        code += "        } else\n";
    }
    if (hasExtension) {
        code += "        if constexpr (std::is_same_v<T, asn1::runtime::ExtensionValue>) {\n";
        code += "            j = nlohmann::json{{\"__extension\", true}};\n";
        code += "        } else\n";
    }
    // Close the if-constexpr chain – need a terminating else {}
    code += "        { (void)alt; } // unreachable\n";
    code += "    }, v);\n";
    code += "}\n\n";

    // ── from_json ─────────────────────────────────────────────────────────────
    // Format: {"altName": value}  — check each known alternative key in turn.
    code += "inline void from_json(const nlohmann::json& j, " + qualifiedTypeName + "& v) {\n";
    for (const auto& a : alts) {
        code += "    if (j.contains(\"" + a.altName + "\")) {\n";
        code += "        " + a.wrapperType + " alt;\n";
        if (a.isNull) {
            code += "        // NULL type – no value to decode\n";
        } else {
            code += "        j.at(\"" + a.altName + "\").get_to(alt." + safeId(a.altName) + ");\n";
        }
        code += "        v = std::move(alt);\n";
        code += "        return;\n";
        code += "    }\n";
    }
    // Unknown key: leave v unchanged (covers ExtensionValue and future extensions)
    code += "    // Unknown alternative – leave v unchanged\n";
    code += "}\n\n";

    return code;
}

// ---------------------------------------------------------------------------
// emitStructAdapterImpl
// ---------------------------------------------------------------------------
// structQualifiedName: the C++ name for the struct inside the current namespace
//   scope, e.g. "MyStruct" or "ParentStruct::nestedField_type".

std::string JsonEmitter::emitStructAdapterImpl(const frontend::AsnNodePtr& assignmentNode,
                                                const std::string& moduleName,
                                                const std::string& structQualifiedName) {
    if (recursion_depth > 20) {
        throw std::runtime_error("Exceeded maximum recursion depth in JsonEmitter::emitStructAdapterImpl for: " +
                                 assignmentNode->name);
    }
    recursion_depth++;

    if (!assignmentNode || assignmentNode->getChildCount() == 0) {
        recursion_depth--;
        return "";
    }

    auto typeDefNode = assignmentNode->getChild(0);
    if (typeDefNode && typeDefNode->resolvedTypeNode)
        typeDefNode = typeDefNode->resolvedTypeNode;

    if (!typeDefNode ||
        (typeDefNode->type != frontend::NodeType::SEQUENCE &&
         typeDefNode->type != frontend::NodeType::SET)) {
        recursion_depth--;
        return "";
    }

    std::string code;

    // ── Pre-pass: emit adapters for inline (anonymous) nested types ──────────
    for (size_t i = 0; i < typeDefNode->getChildCount(); ++i) {
        auto memberNode = typeDefNode->getChild(i);
        if (!memberNode || memberNode->type != frontend::NodeType::ASSIGNMENT) continue;

        auto typeNode = memberNode->getChild(0);
        if (!typeNode) continue;

        // If it has a resolved name, it references another top-level type – skip.
        if (typeNode->resolvedName.has_value()) continue;

        auto effectiveTypeNode = typeNode;
        if (typeNode->resolvedTypeNode) effectiveTypeNode = typeNode->resolvedTypeNode;
        if (!effectiveTypeNode) continue;

        const std::string memberIdName = id(memberNode->name);
        const std::string inlineTypeName = structQualifiedName + "::" + memberIdName + "_type";

        if (effectiveTypeNode->type == frontend::NodeType::ENUMERATION) {
            // Inline enum – emit NLOHMANN_JSON_SERIALIZE_ENUM before the struct adapter.
            code += emitEnumMacro(effectiveTypeNode, inlineTypeName);
        } else if (effectiveTypeNode->type == frontend::NodeType::CHOICE) {
            // Inline CHOICE – emit choice adapter before the struct adapter.
            // Wrapper structs live inside the parent struct, so qualify them with
            // structQualifiedName::fieldName_<altName>.
            const std::string choiceWrapperPrefix = structQualifiedName + "::" + memberIdName;
            code += emitChoiceAdapterImpl(effectiveTypeNode, inlineTypeName, choiceWrapperPrefix);
        } else if (effectiveTypeNode->type == frontend::NodeType::SEQUENCE ||
                   effectiveTypeNode->type == frontend::NodeType::SET) {
            // Inline nested SEQUENCE – emit a struct adapter for it first.
            std::string nestedName = memberIdName + "_type";
            auto dummyAssignment = std::make_shared<frontend::AsnNode>(
                frontend::NodeType::ASSIGNMENT, nestedName, effectiveTypeNode->location);
            dummyAssignment->addChild(effectiveTypeNode);
            code += emitStructAdapterImpl(dummyAssignment, moduleName, inlineTypeName);
        } else if (effectiveTypeNode->type == frontend::NodeType::SEQUENCE_OF ||
                   effectiveTypeNode->type == frontend::NodeType::SET_OF) {
            // If the element type itself is an inline CHOICE or SEQUENCE, emit its adapter.
            if (effectiveTypeNode->getChildCount() > 0) {
                auto elemNode = effectiveTypeNode->getChild(0);
                if (!elemNode->resolvedName.has_value()) {
                    const std::string elemTypeName = structQualifiedName + "::" + memberIdName + "_element_type";
                    if (elemNode->type == frontend::NodeType::CHOICE) {
                        const std::string elemWrapperPrefix = structQualifiedName + "::" + memberIdName + "_element";
                        code += emitChoiceAdapterImpl(elemNode, elemTypeName, elemWrapperPrefix);
                    } else if (elemNode->type == frontend::NodeType::SEQUENCE ||
                               elemNode->type == frontend::NodeType::SET) {
                        std::string nestedName = memberIdName + "_element_type";
                        auto dummyAssignment = std::make_shared<frontend::AsnNode>(
                            frontend::NodeType::ASSIGNMENT, nestedName, elemNode->location);
                        dummyAssignment->addChild(elemNode);
                        code += emitStructAdapterImpl(dummyAssignment, moduleName, elemTypeName);
                    }
                }
            }
        }
    }

    // ── Collect fields (skip EXTENSION_MARKER, skip unknown_extensions) ──────
    struct FieldInfo {
        std::string jsonKey;    // original ASN.1 name (with hyphens etc.)
        std::string cppField;   // mangled C++ identifier
        bool isOptional;        // std::optional wrapper
        bool isNullType;        // inner type is std::nullptr_t
    };
    std::vector<FieldInfo> fields;

    bool in_extension = false;
    for (size_t i = 0; i < typeDefNode->getChildCount(); ++i) {
        auto memberNode = typeDefNode->getChild(i);
        if (!memberNode) continue;
        if (memberNode->type == frontend::NodeType::EXTENSION_MARKER) {
            in_extension = true;
            continue;
        }
        if (memberNode->type != frontend::NodeType::ASSIGNMENT) continue;

        const std::string cppField = id(memberNode->name);
        // Skip the synthetic extension storage field.
        if (cppField == "unknown_extensions") continue;

        auto typeNode = memberNode->getChild(0);
        if (!typeNode) continue;

        auto effectiveTypeNode = typeNode;
        if (typeNode->resolvedTypeNode) effectiveTypeNode = typeNode->resolvedTypeNode;

        bool isNull = false;
        if (!typeNode->resolvedName.has_value() && effectiveTypeNode &&
            effectiveTypeNode->type == frontend::NodeType::NULL_TYPE)
            isNull = true;

        FieldInfo fi;
        fi.jsonKey   = memberNode->name;  // keep original (hyphens fine as JSON key)
        fi.cppField  = cppField;
        fi.isOptional = (memberNode->isOptional || in_extension);
        fi.isNullType = isNull;
        fields.push_back(std::move(fi));
    }

    // ── to_json ───────────────────────────────────────────────────────────────
    code += "inline void to_json(nlohmann::json& j, const " + structQualifiedName + "& v) {\n";
    code += "    j = nlohmann::json::object();\n";
    for (const auto& f : fields) {
        if (f.isOptional) {
            code += "    if (v." + f.cppField + ".has_value()) {\n";
            if (f.isNullType) {
                code += "        j[\"" + f.jsonKey + "\"] = nullptr;\n";
            } else {
                code += "        j[\"" + f.jsonKey + "\"] = *v." + f.cppField + ";\n";
            }
            code += "    }\n";
        } else {
            if (f.isNullType) {
                code += "    j[\"" + f.jsonKey + "\"] = nullptr;\n";
            } else {
                code += "    j[\"" + f.jsonKey + "\"] = v." + f.cppField + ";\n";
            }
        }
    }
    code += "}\n\n";

    // ── from_json ─────────────────────────────────────────────────────────────
    code += "inline void from_json(const nlohmann::json& j, " + structQualifiedName + "& v) {\n";
    for (const auto& f : fields) {
        if (f.isOptional) {
            if (f.isNullType) {
                // Optional NULL – presence of key (non-null) is enough.
                code += "    if (j.contains(\"" + f.jsonKey + "\") && !j.at(\"" + f.jsonKey + "\").is_null()) {\n";
                code += "        v." + f.cppField + " = nullptr;\n";
                code += "    }\n";
            } else {
                code += "    if (j.contains(\"" + f.jsonKey + "\") && !j.at(\"" + f.jsonKey + "\").is_null()) {\n";
                code += "        decltype(v." + f.cppField + ")::value_type tmp{};\n";
                code += "        j.at(\"" + f.jsonKey + "\").get_to(tmp);\n";
                code += "        v." + f.cppField + " = std::move(tmp);\n";
                code += "    }\n";
            }
        } else {
            if (f.isNullType) {
                // Mandatory NULL – nothing to decode, skip.
                code += "    // " + f.jsonKey + " is NULL type – no value to decode\n";
            } else {
                code += "    if (j.contains(\"" + f.jsonKey + "\")) {\n";
                code += "        j.at(\"" + f.jsonKey + "\").get_to(v." + f.cppField + ");\n";
                code += "    }\n";
            }
        }
    }
    code += "}\n\n";

    recursion_depth--;
    return code;
}

// ---------------------------------------------------------------------------
// emitTypeAdapter  (top-level dispatch)
// ---------------------------------------------------------------------------

std::string JsonEmitter::emitTypeAdapter(const frontend::AsnNodePtr& assignmentNode,
                                          const std::string& moduleName) {
    if (!assignmentNode || assignmentNode->getChildCount() == 0) return "";

    auto typeDefNode = assignmentNode->getChild(0);
    if (!typeDefNode) return "";

    // Follow a single level of indirection for aliased types.
    auto effectiveTypeNode = typeDefNode;
    if (typeDefNode->resolvedTypeNode) effectiveTypeNode = typeDefNode->resolvedTypeNode;

    const std::string mangledName = id(assignmentNode->name);
    const std::string moduleNs    = id(moduleName);

    // Record for registerTypes.
    registrations.push_back({moduleNs, mangledName});

    switch (effectiveTypeNode->type) {
        case frontend::NodeType::SEQUENCE:
        case frontend::NodeType::SET: {
            // emitStructAdapterImpl handles inline nested type adapters automatically.
            return emitStructAdapterImpl(assignmentNode, moduleName, mangledName);
        }

        case frontend::NodeType::ENUMERATION: {
            return emitEnumMacro(effectiveTypeNode, mangledName);
        }

        case frontend::NodeType::CHOICE: {
            // Wrapper prefix == mangledName (the choice base name, without _type suffix).
            return emitChoiceAdapterImpl(effectiveTypeNode, mangledName, mangledName);
        }

        // SEQUENCE OF / SET OF → typedef to std::vector<T>.
        // nlohmann handles vectors automatically, BUT if the element type is an
        // anonymous (inline) SEQUENCE or CHOICE, CppEmitter named it
        // mangledName + "_element" — we must emit an adapter for it here.
        case frontend::NodeType::SEQUENCE_OF:
        case frontend::NodeType::SET_OF: {
            if (effectiveTypeNode->getChildCount() > 0) {
                auto elemNode = effectiveTypeNode->getChild(0);
                if (elemNode && !elemNode->resolvedName.has_value()) {
                    const std::string elemTypeName = mangledName + "_element";
                    if (elemNode->type == frontend::NodeType::SEQUENCE ||
                        elemNode->type == frontend::NodeType::SET) {
                        auto dummy = std::make_shared<frontend::AsnNode>(
                            frontend::NodeType::ASSIGNMENT, elemTypeName, elemNode->location);
                        dummy->addChild(elemNode);
                        return emitStructAdapterImpl(dummy, moduleName, elemTypeName);
                    } else if (elemNode->type == frontend::NodeType::CHOICE) {
                        return emitChoiceAdapterImpl(elemNode, elemTypeName, elemTypeName);
                    }
                }
            }
            return "";
        }

        // Primitive typedef aliases (INTEGER, BOOLEAN, OCTET_STRING, …) → nlohmann
        // handles the underlying type automatically.  Return empty.
        default:
            return "";
    }
}

// ---------------------------------------------------------------------------
// emitRegisterFunction
// ---------------------------------------------------------------------------

std::string JsonEmitter::emitRegisterFunction() {
    // Build the fully-qualified namespace path prefix, e.g. "asn1::generated".
    std::string nsPrefix = outputNamespace.empty() ? "" : (outputNamespace + "::");

    std::string code;
    code += "inline void registerTypes(\n";
    code += "    std::unordered_map<std::string, std::function<void(const nlohmann::json&, asn1::runtime::BitWriter&)>>& encoders,\n";
    code += "    std::unordered_map<std::string, std::function<nlohmann::json(asn1::runtime::BitReader&)>>& decoders)\n";
    code += "{\n";

    for (const auto& entry : registrations) {
        const std::string key      = entry.moduleNs + "::" + entry.typeName;
        const std::string fullType = nsPrefix + entry.moduleNs + "::" + entry.typeName;
        const std::string encFunc  = nsPrefix + entry.moduleNs + "::encode_" + entry.typeName;
        const std::string decFunc  = nsPrefix + entry.moduleNs + "::decode_" + entry.typeName;

        code += "    encoders[\"" + key + "\"] = [](const nlohmann::json& j, asn1::runtime::BitWriter& bw) {\n";
        code += "        " + fullType + " obj = j.get<" + fullType + ">();\n";
        code += "        " + encFunc + "(bw, obj);\n";
        code += "    };\n";

        code += "    decoders[\"" + key + "\"] = [](asn1::runtime::BitReader& br) -> nlohmann::json {\n";
        code += "        return nlohmann::json(" + decFunc + "(br));\n";
        code += "    };\n";
    }

    code += "}\n";
    return code;
}

} // namespace asn1::codegen
