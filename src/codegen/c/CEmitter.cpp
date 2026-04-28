#include "codegen/c/CEmitter.h"
#include "utils/Logger.h"
#include <algorithm>
#include <unordered_set>

namespace asn1::codegen {

using namespace frontend;

// ── helpers ──────────────────────────────────────────────────────────────

static inline std::string id(const std::string& n) {
    return TypeMap::mangleName(n);
}

// C keywords that can collide with ASN.1 identifiers.
static bool isCKeyword(const std::string& s) {
    static const std::unordered_set<std::string> kw = {
        "auto","break","case","char","const","continue","default","do","double",
        "else","enum","extern","float","for","goto","if","inline","int","long",
        "register","restrict","return","short","signed","sizeof","static","struct",
        "switch","typedef","union","unsigned","void","volatile","while","_Bool",
        "_Complex","_Imaginary"
    };
    return kw.count(s) > 0;
}

static std::string safeId(const std::string& n) {
    std::string s = id(n);
    if (isCKeyword(s)) s += "_";
    return s;
}

// ── static helpers ────────────────────────────────────────────────────────

std::string CEmitter::cName(const std::string& module, const std::string& type) {
    return id(module) + "_" + id(type);
}

std::string CEmitter::cRef(const std::string& resolved, const std::string& cur_module) {
    auto dot = resolved.find('.');
    if (dot == std::string::npos) {
        std::string t = resolved;
        std::replace(t.begin(), t.end(), '-', '_');
        // In C (flat namespace) always qualify with the module prefix.
        return cur_module.empty() ? t : (id(cur_module) + "_" + t);
    }
    std::string mod = resolved.substr(0, dot);
    std::string typ = resolved.substr(dot + 1);
    std::replace(mod.begin(), mod.end(), '-', '_');
    std::replace(typ.begin(), typ.end(), '-', '_');
    return mod + "_" + typ;
}

// ── constructor ───────────────────────────────────────────────────────────

CEmitter::CEmitter() {}

void CEmitter::setContext(const SymbolTable& table, const std::string& mod) {
    symbol_table   = &table;
    current_module = mod;
}

// ── preamble / epilogue ───────────────────────────────────────────────────

std::string CEmitter::emitHeaderPreamble(const std::string& guard) {
    std::string h;
    h += "#ifndef " + guard + "\n";
    h += "#define " + guard + "\n\n";
    h += "#include <stddef.h>\n";
    h += "#include <stdint.h>\n";
    h += "#include \"runtime/c/asn1_bitwriter.h\"\n";
    h += "#include \"runtime/c/asn1_bitreader.h\"\n\n";
    h += "#ifdef __cplusplus\nextern \"C\" {\n#endif\n\n";
    return h;
}

std::string CEmitter::emitHeaderEpilogue(const std::string& guard) {
    return "\n#ifdef __cplusplus\n}\n#endif\n\n#endif /* " + guard + " */\n";
}

// ── cTypeFor ──────────────────────────────────────────────────────────────
// Returns the C type string for a SEQUENCE member.
// For anonymous inline complex types, pre-emits a typedef into `pre_emit`.

std::string CEmitter::cTypeFor(const AsnNodePtr& member_node,
                                const std::string& module_name,
                                const std::string& owner_name,
                                const std::string& field_name,
                                std::string& pre_emit) {
    // Cross-module reference wins over everything
    if (member_node->resolvedName.has_value())
        return cRef(member_node->resolvedName.value(), module_name);

    // Unwrap: resolvedTypeNode > first child > the node itself
    AsnNodePtr eff = member_node->resolvedTypeNode
        ? member_node->resolvedTypeNode
        : (member_node->getChildCount() > 0 ? member_node->getChild(0) : member_node);

    if (!eff) return "int /* unknown */";

    // If the unwrapped node carries a named reference that was fully resolved
    // (i.e. resolvedTypeNode is available), use the C name for that reference.
    // If resolvedTypeNode is null the name is a type parameter — use a placeholder.
    if (eff->resolvedName.has_value()) {
        if (eff->resolvedTypeNode) {
            // If the resolved symbol is a parameterized template, resolvedTypeNode
            // is an inline instantiation — emit it inline rather than by template name.
            if (symbol_table) {
                const auto& rn = eff->resolvedName.value();
                auto dot = rn.find('.');
                if (dot != std::string::npos) {
                    auto sym = symbol_table->lookupSymbol(rn.substr(0, dot), rn.substr(dot + 1));
                    if (sym && sym->isParameterized) {
                        std::string inner_name = cName(module_name, owner_name + "_" + field_name + "_type");
                        AsnNodePtr dummy = std::make_shared<AsnNode>(NodeType::ASSIGNMENT,
                            owner_name + "-" + field_name + "-type", eff->resolvedTypeNode->location);
                        dummy->addChild(eff->resolvedTypeNode);
                        switch (eff->resolvedTypeNode->type) {
                            case NodeType::SEQUENCE: case NodeType::SET:
                                pre_emit += emitStruct(dummy, module_name); break;
                            case NodeType::CHOICE:
                                pre_emit += emitChoice(dummy, module_name); break;
                            case NodeType::SEQUENCE_OF: case NodeType::SET_OF:
                                pre_emit += emitSequenceOf(dummy, module_name); break;
                            default: break;
                        }
                        return inner_name;
                    }
                }
            }
            return cRef(eff->resolvedName.value(), module_name);
        }
        // Unresolved type parameter — emit a generic pointer placeholder.
        return "uint8_t* /* type parameter */";
    }

    switch (eff->type) {
        case NodeType::INTEGER:
            return "int64_t";
        case NodeType::BOOLEAN:
            return "int";
        case NodeType::NULL_TYPE:
            return "int /* NULL */";
        case NodeType::REAL:
            return "double";
        case NodeType::OBJECT_IDENTIFIER:
            return "uint8_t* /* OID */";
        case NodeType::BIT_STRING: {
            // Anonymous BIT STRING field: define a local struct typedef
            std::string inner = cName(module_name, owner_name + "_" + field_name);
            pre_emit += "typedef struct { uint8_t* data; size_t bit_length; } " + inner + ";\n";
            return inner;
        }
        case NodeType::OCTET_STRING:
        case NodeType::UTF8_STRING:
        case NodeType::PRINTABLE_STRING:
        case NodeType::VISIBLE_STRING:
        case NodeType::IA5_STRING:
        case NodeType::NUMERIC_STRING: {
            std::string inner = cName(module_name, owner_name + "_" + field_name);
            pre_emit += "typedef struct { uint8_t* data; size_t length; } " + inner + ";\n";
            return inner;
        }
        case NodeType::ENUMERATION: {
            // Inline enum — hoist it out
            std::string enum_name = cName(module_name, owner_name + "_" + field_name + "_type");
            // Build a dummy assignment node to reuse emitEnum
            AsnNodePtr dummy = std::make_shared<AsnNode>(NodeType::ASSIGNMENT, owner_name + "-" + field_name + "-type", eff->location);
            dummy->addChild(std::make_shared<AsnNode>(*eff));
            pre_emit += emitEnum(dummy, module_name);
            return enum_name;
        }
        case NodeType::SEQUENCE:
        case NodeType::SET: {
            std::string inner_name = cName(module_name, owner_name + "_" + field_name + "_type");
            AsnNodePtr dummy = std::make_shared<AsnNode>(NodeType::ASSIGNMENT, owner_name + "-" + field_name + "-type", eff->location);
            dummy->addChild(std::make_shared<AsnNode>(*eff));
            pre_emit += emitStruct(dummy, module_name);
            return inner_name;
        }
        case NodeType::SEQUENCE_OF:
        case NodeType::SET_OF: {
            std::string inner_name = cName(module_name, owner_name + "_" + field_name + "_type");
            AsnNodePtr dummy = std::make_shared<AsnNode>(NodeType::ASSIGNMENT, owner_name + "-" + field_name + "-type", eff->location);
            dummy->addChild(std::make_shared<AsnNode>(*eff));
            pre_emit += emitSequenceOf(dummy, module_name);
            return inner_name;
        }
        case NodeType::CHOICE: {
            std::string inner_name = cName(module_name, owner_name + "_" + field_name + "_type");
            AsnNodePtr dummy = std::make_shared<AsnNode>(NodeType::ASSIGNMENT, owner_name + "-" + field_name + "-type", eff->location);
            dummy->addChild(std::make_shared<AsnNode>(*eff));
            pre_emit += emitChoice(dummy, module_name);
            return inner_name;
        }
        case NodeType::IDENTIFIER: {
            // Prefer the resolved fully-qualified name so the module prefix is included.
            if (eff->resolvedName.has_value())
                return cRef(eff->resolvedName.value(), module_name);
            return cRef(eff->name, module_name);
        }
        default:
            return "uint8_t* /* unsupported */";
    }
}

// ── emitStruct ────────────────────────────────────────────────────────────

std::string CEmitter::emitStruct(const AsnNodePtr& node,
                                  const std::string& module_name) {
    if (recursion_depth > 20) return "";
    recursion_depth++;

    auto type_node = node->getChild(0);
    if (!type_node ||
        (type_node->type != NodeType::SEQUENCE && type_node->type != NodeType::SET)) {
        recursion_depth--;
        return "";
    }

    std::string type_c = cName(module_name, node->name);
    std::string pre_emit;
    std::string fields;

    // Count optional fields for a comment
    bool has_extension = type_node->hasExtension;
    int opt_count = 0;

    for (size_t i = 0; i < type_node->getChildCount(); ++i) {
        auto member = type_node->getChild(i);
        if (!member || member->type == NodeType::EXTENSION_MARKER) continue;
        if (member->type != NodeType::ASSIGNMENT) continue;

        bool optional = member->isOptional || member->hasDefault;
        if (optional) opt_count++;

        auto eff = member->resolvedTypeNode ? member->resolvedTypeNode : member->getChild(0);
        std::string field_c;

        if (member->resolvedName.has_value()) {
            field_c = cRef(member->resolvedName.value(), module_name);
        } else if (eff && eff->resolvedName.has_value()) {
            // Check if the resolved symbol is a parameterized template; if so,
            // eff->resolvedTypeNode is an inline instantiation and must be emitted inline.
            bool is_param_inst = false;
            if (eff->resolvedTypeNode && symbol_table) {
                const auto& rn = eff->resolvedName.value();
                auto dot = rn.find('.');
                if (dot != std::string::npos) {
                    auto sym = symbol_table->lookupSymbol(rn.substr(0, dot), rn.substr(dot + 1));
                    if (sym && sym->isParameterized) is_param_inst = true;
                }
            }
            if (is_param_inst)
                field_c = cTypeFor(member, module_name, id(node->name), safeId(member->name), pre_emit);
            else
                field_c = cRef(eff->resolvedName.value(), module_name);
        } else if (eff) {
            field_c = cTypeFor(member, module_name, id(node->name), safeId(member->name), pre_emit);
        } else {
            field_c = "int /* unknown */";
        }

        if (optional) {
            fields += "    int has_" + safeId(member->name) + ";\n";
        }
        fields += "    " + field_c + " " + safeId(member->name) + ";\n";
    }

    // Extension blob placeholder (if extensible)
    if (has_extension) {
        fields += "    uint8_t* _ext_data;\n";
        fields += "    size_t   _ext_len;\n";
    }

    std::string code = pre_emit;
    code += "typedef struct {\n";
    code += fields.empty() ? "    int _placeholder;\n" : fields;
    code += "} " + type_c + ";\n\n";

    recursion_depth--;
    return code;
}

// ── emitEnum ─────────────────────────────────────────────────────────────

std::string CEmitter::emitEnum(const AsnNodePtr& node,
                                const std::string& module_name) {
    auto type_node = node->getChild(0);
    if (!type_node || type_node->type != NodeType::ENUMERATION) return "";

    std::string type_c = cName(module_name, node->name);
    std::string code = "typedef enum {\n";

    bool has_ext = false;
    int idx = 0;
    for (size_t i = 0; i < type_node->getChildCount(); ++i) {
        auto e = type_node->getChild(i);
        if (!e) continue;
        if (e->type == NodeType::EXTENSION_MARKER) { has_ext = true; continue; }
        code += "    " + type_c + "_" + safeId(e->name) + " = " + std::to_string(idx++) + ",\n";
    }
    if (has_ext)
        code += "    " + type_c + "_UNKNOWN = -1,\n";

    code += "} " + type_c + ";\n\n";
    return code;
}

// ── emitChoice ───────────────────────────────────────────────────────────

std::string CEmitter::emitChoice(const AsnNodePtr& node,
                                  const std::string& module_name) {
    if (recursion_depth > 20) return "";
    recursion_depth++;

    auto type_node = node->getChild(0);
    if (!type_node || type_node->type != NodeType::CHOICE) {
        recursion_depth--;
        return "";
    }

    std::string type_c = cName(module_name, node->name);
    std::string pre_emit;
    bool has_ext = false;

    // Collect alternatives
    struct Alt { std::string c_type; std::string field_name; int idx; };
    std::vector<Alt> alts;

    int base_count = 0;
    for (size_t i = 0; i < type_node->getChildCount(); ++i) {
        auto opt = type_node->getChild(i);
        if (!opt) continue;
        if (opt->type == NodeType::EXTENSION_MARKER) { has_ext = true; continue; }
        if (opt->type != NodeType::ASSIGNMENT) continue;

        std::string alt_type;
        if (opt->resolvedName.has_value()) {
            // Verify the referenced type actually exists in the symbol table.
            // If it doesn't (e.g. formal type parameter like ElementType), use a placeholder.
            bool exists = false;
            const auto& rn = opt->resolvedName.value();
            if (symbol_table) {
                auto dot = rn.find('.');
                if (dot != std::string::npos) {
                    auto sym = symbol_table->lookupSymbol(rn.substr(0, dot),
                                                          rn.substr(dot + 1));
                    exists = (sym != nullptr);
                    // If the symbol exists but is a parameterized type template,
                    // it's not a concrete type for use as a field — treat as placeholder.
                    if (sym && sym->isParameterized) exists = false;
                }
            } else {
                exists = (opt->resolvedTypeNode != nullptr);
            }
            alt_type = exists ? cRef(rn, module_name)
                              : "uint8_t* /* type parameter */";
        } else {
            alt_type = cTypeFor(opt, module_name,
                                id(node->name), safeId(opt->name), pre_emit);
        }
        alts.push_back({alt_type, safeId(opt->name), base_count++});
    }

    // Tag enum
    std::string tag_enum = type_c + "_TAG";
    std::string code = pre_emit;
    code += "typedef enum {\n";
    for (const auto& a : alts)
        code += "    " + tag_enum + "_" + a.field_name + " = " + std::to_string(a.idx) + ",\n";
    if (has_ext)
        code += "    " + tag_enum + "_UNKNOWN = -1,\n";
    code += "} " + tag_enum + ";\n\n";

    // Tagged-union struct
    code += "typedef struct {\n";
    code += "    " + tag_enum + " tag;\n";
    code += "    union {\n";
    for (const auto& a : alts)
        code += "        " + a.c_type + " " + a.field_name + ";\n";
    code += "    } u;\n";
    code += "} " + type_c + ";\n\n";

    recursion_depth--;
    return code;
}

// ── emitTypedef ───────────────────────────────────────────────────────────

std::string CEmitter::emitTypedef(const AsnNodePtr& node,
                                   const std::string& module_name) {
    auto type_node = node->getChild(0);
    if (!type_node) return "";

    std::string type_c = cName(module_name, node->name);

    // Cross-module alias
    if (type_node->resolvedName.has_value()) {
        std::string ref = cRef(type_node->resolvedName.value(), module_name);
        return "typedef " + ref + " " + type_c + ";\n\n";
    }

    switch (type_node->type) {
        case NodeType::INTEGER:
            return "typedef int64_t " + type_c + ";\n\n";
        case NodeType::BOOLEAN:
            return "typedef int " + type_c + ";\n\n";
        case NodeType::REAL:
            return "typedef double " + type_c + ";\n\n";
        case NodeType::NULL_TYPE:
            return "typedef int " + type_c + "; /* NULL */\n\n";
        case NodeType::OBJECT_IDENTIFIER:
            return "/* OID typedef omitted: " + type_c + " */\n\n";
        case NodeType::BIT_STRING:
            return "typedef struct { uint8_t* data; size_t bit_length; } " + type_c + ";\n\n";
        case NodeType::OCTET_STRING:
        case NodeType::UTF8_STRING:
        case NodeType::PRINTABLE_STRING:
        case NodeType::VISIBLE_STRING:
        case NodeType::IA5_STRING:
        case NodeType::NUMERIC_STRING:
            return "typedef struct { uint8_t* data; size_t length; } " + type_c + ";\n\n";
        case NodeType::SEQUENCE:
        case NodeType::SET:
            return emitStruct(node, module_name);
        case NodeType::SEQUENCE_OF:
        case NodeType::SET_OF:
            return emitSequenceOf(node, module_name);
        case NodeType::CHOICE:
            return emitChoice(node, module_name);
        case NodeType::ENUMERATION:
            return emitEnum(node, module_name);
        case NodeType::IDENTIFIER: {
            std::string ref = id(type_node->name);
            if (ref == type_c) return "";
            return "typedef " + ref + " " + type_c + ";\n\n";
        }
        default:
            return "";
    }
}

// ── emitSequenceOf ────────────────────────────────────────────────────────

std::string CEmitter::emitSequenceOf(const AsnNodePtr& node,
                                      const std::string& module_name) {
    auto type_node = node->getChild(0);
    if (!type_node ||
        (type_node->type != NodeType::SEQUENCE_OF &&
         type_node->type != NodeType::SET_OF)) return "";

    std::string type_c = cName(module_name, node->name);
    std::string pre_emit;

    // Determine element C type
    std::string elem_type = "int /* unknown */";
    if (!type_node->children.empty()) {
        auto elem = type_node->getChild(0);
        if (elem) {
            if (elem->resolvedName.has_value()) {
                elem_type = cRef(elem->resolvedName.value(), module_name);
            } else if (elem->resolvedTypeNode) {
                AsnNodePtr dummy = std::make_shared<AsnNode>(NodeType::ASSIGNMENT,
                    node->name + "-elem", elem->location);
                dummy->addChild(elem->resolvedTypeNode);
                // Just use the type string
                std::string dummy_pre;
                elem_type = cTypeFor(elem, module_name, id(node->name), "elem", dummy_pre);
                pre_emit += dummy_pre;
            } else {
                switch (elem->type) {
                    case NodeType::INTEGER:      elem_type = "int64_t"; break;
                    case NodeType::BOOLEAN:      elem_type = "int"; break;
                    case NodeType::OCTET_STRING: elem_type = cName(module_name, node->name + "_elem"); break;
                    case NodeType::BIT_STRING:   elem_type = cName(module_name, node->name + "_elem"); break;
                    default: {
                        std::string dummy_pre;
                        elem_type = cTypeFor(elem, module_name, id(node->name), "elem", dummy_pre);
                        pre_emit += dummy_pre;
                        break;
                    }
                }
            }
        }
    }

    std::string code = pre_emit;
    code += "typedef struct {\n";
    code += "    " + elem_type + "* items;\n";
    code += "    size_t count;\n";
    code += "} " + type_c + ";\n\n";
    return code;
}

// ── emitValueAssignment ───────────────────────────────────────────────────

std::string CEmitter::emitValueAssignment(const AsnNodePtr& node,
                                           const std::string& module_name) {
    if (!node || node->type != NodeType::VALUE_ASSIGNMENT) return "";
    if (!node->value.has_value()) return "";
    std::string macro_name = cName(module_name, node->name);
    return "#define " + macro_name + " " + node->value.value() + "\n";
}

} // namespace asn1::codegen
