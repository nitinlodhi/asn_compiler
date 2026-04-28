#include "codegen/c/CCodecEmitter.h"
#include "frontend/ConstraintResolver.h"
#include "utils/Logger.h"
#include <algorithm>
#include <cmath>
#include <sstream>
#include <unordered_set>

namespace asn1::codegen {

using namespace frontend;

// ── helpers ───────────────────────────────────────────────────────────────

static inline std::string id(const std::string& n) {
    return TypeMap::mangleName(n);
}

static std::string safeId(const std::string& n) {
    static const std::unordered_set<std::string> kw = {
        "auto","break","case","char","const","continue","default","do","double",
        "else","enum","extern","float","for","goto","if","inline","int","long",
        "register","restrict","return","short","signed","sizeof","static","struct",
        "switch","typedef","union","unsigned","void","volatile","while"
    };
    std::string s = id(n);
    if (kw.count(s)) s += "_";
    return s;
}

std::string CCodecEmitter::cName(const std::string& module,
                                  const std::string& type) {
    return id(module) + "_" + id(type);
}

std::string CCodecEmitter::cRef(const std::string& resolved,
                                 const std::string& cur_module) {
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

bool CCodecEmitter::isScalarCType(const AsnNodePtr& type_node) {
    if (!type_node) return false;
    switch (type_node->type) {
        case NodeType::INTEGER:
        case NodeType::BOOLEAN:
        case NodeType::REAL:
        case NodeType::NULL_TYPE:
        case NodeType::ENUMERATION:
            return true;
        default:
            return false;
    }
}

static bool isParameterizedRef(const frontend::SymbolTable* table, const std::string& resolved_name) {
    if (!table) return false;
    auto dot = resolved_name.find('.');
    if (dot == std::string::npos) return false;
    auto sym = table->lookupSymbol(resolved_name.substr(0, dot), resolved_name.substr(dot + 1));
    return sym && sym->isParameterized;
}

static int rangeBitsFor(int64_t min_v, int64_t max_v) {
    if (min_v >= max_v) return 0;
    int64_t range = max_v - min_v + 1;
    int bits = 0;
    int64_t v = range - 1;
    while (v > 0) { bits++; v >>= 1; }
    return bits;
}

// ── constructor / context ─────────────────────────────────────────────────

CCodecEmitter::CCodecEmitter() {}

void CCodecEmitter::setContext(const SymbolTable& table,
                                const std::string& mod) {
    symbol_table   = &table;
    current_module = mod;
}

// ── resolveEffectiveType ──────────────────────────────────────────────────

AsnNodePtr CCodecEmitter::resolveEffectiveType(const AsnNodePtr& node) {
    if (!node) return nullptr;
    AsnNodePtr eff = node->resolvedTypeNode ? node->resolvedTypeNode : node->getChild(0);
    if (!eff) return node;
    // Walk alias chains
    for (int depth = 0; depth < 10; ++depth) {
        if (eff->resolvedTypeNode) { eff = eff->resolvedTypeNode; continue; }
        if (eff->type == NodeType::IDENTIFIER && symbol_table && eff->resolvedName.has_value()) {
            auto dot = eff->resolvedName->find('.');
            if (dot != std::string::npos) {
                auto next = symbol_table->lookupSymbol(
                    eff->resolvedName->substr(0, dot),
                    eff->resolvedName->substr(dot + 1));
                if (next && next->getChildCount() > 0) {
                    eff = next->getChild(0);
                    continue;
                }
            }
        }
        break;
    }
    return eff;
}

// ── encode/decode call helpers ────────────────────────────────────────────

std::string CCodecEmitter::encodeCallFor(const std::string& prefix,
                                          const std::string& type_name,
                                          const std::string& expr,
                                          bool is_scalar) {
    std::string fn = prefix + "_encode_" + type_name;
    std::string arg = is_scalar ? expr : "&(" + expr + ")";
    return "rc = " + fn + "(writer, " + arg + ", err_buf, err_buf_len);\n"
           "    if (rc != 0) return rc;\n";
}

std::string CCodecEmitter::decodeCallFor(const std::string& prefix,
                                          const std::string& type_name,
                                          const std::string& out_expr,
                                          bool is_scalar) {
    std::string fn = prefix + "_decode_" + type_name;
    std::string arg = is_scalar ? out_expr : out_expr;
    return "rc = " + fn + "(reader, " + arg + ", err_buf, err_buf_len);\n"
           "    if (rc != 0) return rc;\n";
}

// ── error helper (inline in generated code) ───────────────────────────────


// ── declarations ──────────────────────────────────────────────────────────

static std::string encDecSig(const std::string& fn_name,
                              const std::string& c_type,
                              bool is_scalar,
                              bool is_enc) {
    if (is_enc) {
        std::string arg = is_scalar
            ? c_type + " value"
            : "const " + c_type + "* value";
        return "int " + fn_name + "(Asn1BitWriter* writer, " + arg +
               ", char* err_buf, size_t err_buf_len)";
    } else {
        std::string arg = is_scalar
            ? c_type + "* out"
            : c_type + "* out";
        return "int " + fn_name + "(Asn1BitReader* reader, " + arg +
               ", char* err_buf, size_t err_buf_len)";
    }
}

std::string CCodecEmitter::emitEncoderDeclaration(const AsnNodePtr& node,
                                                    const std::string& module) {
    auto type_node = node->getChild(0);
    if (!type_node) return "";
    std::string c_type  = cName(module, node->name);
    std::string fn_name = cName(module, "encode_" + node->name);
    bool scalar = isScalarCType(type_node);
    return encDecSig(fn_name, c_type, scalar, true) + ";\n";
}

std::string CCodecEmitter::emitDecoderDeclaration(const AsnNodePtr& node,
                                                    const std::string& module) {
    auto type_node = node->getChild(0);
    if (!type_node) return "";
    std::string c_type  = cName(module, node->name);
    std::string fn_name = cName(module, "decode_" + node->name);
    bool scalar = isScalarCType(type_node);
    return encDecSig(fn_name, c_type, scalar, false) + ";\n";
}

// ── definition entry points ───────────────────────────────────────────────

std::string CCodecEmitter::emitEncoderDefinition(const AsnNodePtr& node,
                                                   const std::string& module) {
    auto raw_type = node->getChild(0);
    if (!raw_type) return "";
    auto type_node = resolveEffectiveType(node);
    if (!type_node) type_node = raw_type;

    std::string c_type  = cName(module, node->name);
    std::string fn_name = cName(module, "encode_" + node->name);
    bool scalar = isScalarCType(raw_type);

    std::string sig = encDecSig(fn_name, c_type, scalar, true);
    std::string body;
    std::string var = scalar ? "value" : "(*value)";

    switch (type_node->type) {
        case NodeType::INTEGER:
            body = generateIntegerLogic(type_node, var, true, c_type);
            break;
        case NodeType::BOOLEAN:
            body = generateBooleanLogic(var, true);
            break;
        case NodeType::BIT_STRING:
            body = generateBitStringLogic(type_node, var, true, c_type);
            break;
        case NodeType::OCTET_STRING:
            body = generateOctetStringLogic(type_node, var, true, c_type);
            break;
        case NodeType::UTF8_STRING:
        case NodeType::PRINTABLE_STRING:
        case NodeType::VISIBLE_STRING:
        case NodeType::IA5_STRING:
        case NodeType::NUMERIC_STRING:
            body = generateCharStringLogic(type_node, var, true, c_type);
            break;
        case NodeType::ENUMERATION:
            body = generateEnumeratedLogic(type_node, var, true);
            break;
        case NodeType::SEQUENCE:
        case NodeType::SET:
            body = generateSequenceLogic(node, module, c_type, true);
            break;
        case NodeType::CHOICE:
            body = generateChoiceLogic(node, module, c_type, true);
            break;
        case NodeType::SEQUENCE_OF:
        case NodeType::SET_OF:
            body = generateSequenceOfLogic(node, module, c_type, true);
            break;
        default:
            // For unresolved aliases: call through
            if (raw_type->resolvedName.has_value()) {
                std::string ref = cRef(raw_type->resolvedName.value(), module);
                bool ref_scalar = (type_node->type == NodeType::INTEGER ||
                                   type_node->type == NodeType::BOOLEAN ||
                                   type_node->type == NodeType::ENUMERATION);
                body = "    int rc;\n    " + encodeCallFor(ref.substr(0, ref.rfind('_')),
                    ref.substr(ref.rfind('_') + 1), var, ref_scalar);
                body += "    return rc;\n";
            } else {
                body = "    (void)writer; (void)err_buf; (void)err_buf_len;\n    return 0;\n";
            }
            break;
    }

    return sig + " {\n" + body + "}\n\n";
}

std::string CCodecEmitter::emitDecoderDefinition(const AsnNodePtr& node,
                                                   const std::string& module) {
    auto raw_type = node->getChild(0);
    if (!raw_type) return "";
    auto type_node = resolveEffectiveType(node);
    if (!type_node) type_node = raw_type;

    std::string c_type  = cName(module, node->name);
    std::string fn_name = cName(module, "decode_" + node->name);
    bool scalar = isScalarCType(raw_type);

    std::string sig = encDecSig(fn_name, c_type, scalar, false);
    std::string body;
    std::string var = scalar ? "(*out)" : "(*out)";

    switch (type_node->type) {
        case NodeType::INTEGER:
            body = generateIntegerLogic(type_node, var, false, c_type);
            break;
        case NodeType::BOOLEAN:
            body = generateBooleanLogic(var, false);
            break;
        case NodeType::BIT_STRING:
            body = generateBitStringLogic(type_node, var, false, c_type);
            break;
        case NodeType::OCTET_STRING:
            body = generateOctetStringLogic(type_node, var, false, c_type);
            break;
        case NodeType::UTF8_STRING:
        case NodeType::PRINTABLE_STRING:
        case NodeType::VISIBLE_STRING:
        case NodeType::IA5_STRING:
        case NodeType::NUMERIC_STRING:
            body = generateCharStringLogic(type_node, var, false, c_type);
            break;
        case NodeType::ENUMERATION:
            body = generateEnumeratedLogic(type_node, var, false);
            break;
        case NodeType::SEQUENCE:
        case NodeType::SET:
            body = generateSequenceLogic(node, module, c_type, false);
            break;
        case NodeType::CHOICE:
            body = generateChoiceLogic(node, module, c_type, false);
            break;
        case NodeType::SEQUENCE_OF:
        case NodeType::SET_OF:
            body = generateSequenceOfLogic(node, module, c_type, false);
            break;
        default:
            body = "    (void)reader; (void)err_buf; (void)err_buf_len;\n    return 0;\n";
            break;
    }

    return sig + " {\n" + body + "}\n\n";
}

// ── INTEGER ───────────────────────────────────────────────────────────────

std::string CCodecEmitter::generateIntegerLogic(const AsnNodePtr& type_node,
                                                  const std::string& var,
                                                  bool is_enc,
                                                  const std::string& prefix) {
    // Extract constraint
    int64_t min_v = INT64_MIN, max_v = INT64_MAX;
    bool constrained = false;
    if (symbol_table) {
        auto typeInfo = ConstraintResolver::resolveConstraints(type_node, *symbol_table, current_module);
        if (typeInfo && typeInfo->minValue.has_value() && typeInfo->maxValue.has_value()) {
            min_v = (int64_t)typeInfo->minValue.value();
            max_v = (int64_t)typeInfo->maxValue.value();
            constrained = true;
        }
    }

    auto sv = [](int64_t v){ return std::to_string((long long)v) + "LL"; };
    std::string code = "    int rc = 0; (void)rc;\n";

    if (is_enc) {
        if (constrained) {
            code += "    if (" + var + " < " + sv(min_v) + " || " + var + " > " + sv(max_v) + ") {\n";
            code += "        char _msg[128];\n";
            code += "        snprintf(_msg, sizeof(_msg), \"INTEGER constraint violation: value %lld out of range ["
                    + std::to_string((long long)min_v) + ", " + std::to_string((long long)max_v)
                    + "].\", (long long)" + var + ");\n";
            code += "        if (err_buf && err_buf_len > 0) snprintf(err_buf, err_buf_len, \"%s\", _msg);\n";
            code += "        return -1;\n    }\n";
            code += "    asn1_uper_encode_constrained_int(writer, (int64_t)" + var + ", " + sv(min_v) + ", " + sv(max_v) + ");\n";
        } else {
            code += "    asn1_uper_encode_unconstrained_int(writer, (int64_t)" + var + ");\n";
        }
        code += "    return 0;\n";
    } else {
        if (constrained) {
            code += "    " + var + " = (" + prefix + ")asn1_uper_decode_constrained_int(reader, " + sv(min_v) + ", " + sv(max_v) + ");\n";
            code += "    if (" + var + " < " + sv(min_v) + " || " + var + " > " + sv(max_v) + ") {\n";
            code += "        char _msg[128];\n";
            code += "        snprintf(_msg, sizeof(_msg), \"INTEGER constraint violation: decoded value %lld out of range ["
                    + std::to_string((long long)min_v) + ", " + std::to_string((long long)max_v)
                    + "].\", (long long)" + var + ");\n";
            code += "        if (err_buf && err_buf_len > 0) snprintf(err_buf, err_buf_len, \"%s\", _msg);\n";
            code += "        return -1;\n    }\n";
        } else {
            code += "    " + var + " = (" + prefix + ")asn1_uper_decode_unconstrained_int(reader);\n";
        }
        code += "    return 0;\n";
    }
    return code;
}

// ── BOOLEAN ───────────────────────────────────────────────────────────────

std::string CCodecEmitter::generateBooleanLogic(const std::string& var,
                                                  bool is_enc) {
    std::string code = "    int rc = 0; (void)rc;\n";
    code += "    (void)err_buf; (void)err_buf_len;\n";
    if (is_enc) {
        code += "    asn1_bw_write_bits(writer, " + var + " ? 1u : 0u, 1);\n";
        code += "    return 0;\n";
    } else {
        code += "    " + var + " = (int)asn1_br_read_bits(reader, 1);\n";
        code += "    return 0;\n";
    }
    return code;
}

// ── BIT STRING ────────────────────────────────────────────────────────────

std::string CCodecEmitter::generateBitStringLogic(const AsnNodePtr& type_node,
                                                    const std::string& var,
                                                    bool is_enc,
                                                    const std::string& prefix) {
    int64_t min_s = 0, max_s = INT64_MAX;
    bool constrained = false;
    if (symbol_table) {
        auto typeInfo = ConstraintResolver::resolveConstraints(type_node, *symbol_table, current_module);
        if (typeInfo && typeInfo->minValue.has_value() && typeInfo->maxValue.has_value()) {
            min_s = (int64_t)typeInfo->minValue.value();
            max_s = (int64_t)typeInfo->maxValue.value();
            constrained = true;
        }
    }

    auto sv = [](int64_t v){ return std::to_string((long long)v) + "LL"; };
    std::string code = "    int rc = 0; (void)rc;\n";

    if (is_enc) {
        if (constrained && min_s == max_s) {
            code += "    if (" + var + ".bit_length != (size_t)" + sv(min_s) + ") {\n";
            code += "        char _msg[128];\n";
            code += "        snprintf(_msg, sizeof(_msg), \"BIT STRING SIZE constraint violation: length %zu != "
                    + std::to_string((long long)min_s) + ".\", " + var + ".bit_length);\n";
            code += "        if (err_buf && err_buf_len > 0) snprintf(err_buf, err_buf_len, \"%s\", _msg);\n";
            code += "        return -1;\n    }\n";
            code += "    asn1_bw_write_bytes(writer, " + var + ".data, (size_t)" + sv(min_s) + ");\n";
        } else if (constrained) {
            code += "    if (" + var + ".bit_length < (size_t)" + sv(min_s) + " || " + var + ".bit_length > (size_t)" + sv(max_s) + ") {\n";
            code += "        char _msg[128];\n";
            code += "        snprintf(_msg, sizeof(_msg), \"BIT STRING SIZE constraint violation: length %zu out of range ["
                    + std::to_string((long long)min_s) + ", " + std::to_string((long long)max_s)
                    + "].\", " + var + ".bit_length);\n";
            code += "        if (err_buf && err_buf_len > 0) snprintf(err_buf, err_buf_len, \"%s\", _msg);\n";
            code += "        return -1;\n    }\n";
            code += "    asn1_uper_encode_length(writer, " + var + ".bit_length, (size_t)" + sv(min_s) + ", (size_t)" + sv(max_s) + ");\n";
            code += "    asn1_bw_write_bytes(writer, " + var + ".data, " + var + ".bit_length);\n";
        } else {
            code += "    asn1_uper_encode_unconstrained_length(writer, " + var + ".bit_length);\n";
            code += "    asn1_bw_write_bytes(writer, " + var + ".data, " + var + ".bit_length);\n";
        }
        code += "    return 0;\n";
    } else {
        if (constrained && min_s == max_s) {
            code += "    " + var + ".bit_length = (size_t)" + sv(min_s) + ";\n";
            code += "    " + var + ".data = (uint8_t*)malloc((" + sv(min_s) + " + 7) / 8);\n";
            code += "    if (!" + var + ".data) return -1;\n";
            code += "    asn1_br_read_bytes(reader, " + var + ".data, (size_t)" + sv(min_s) + ");\n";
        } else if (constrained) {
            code += "    " + var + ".bit_length = asn1_uper_decode_length(reader, (size_t)" + sv(min_s) + ", (size_t)" + sv(max_s) + ");\n";
            code += "    " + var + ".data = (uint8_t*)malloc((" + var + ".bit_length + 7) / 8);\n";
            code += "    if (!" + var + ".data) return -1;\n";
            code += "    asn1_br_read_bytes(reader, " + var + ".data, " + var + ".bit_length);\n";
        } else {
            code += "    " + var + ".bit_length = asn1_uper_decode_unconstrained_length(reader);\n";
            code += "    " + var + ".data = (uint8_t*)malloc((" + var + ".bit_length + 7) / 8);\n";
            code += "    if (!" + var + ".data) return -1;\n";
            code += "    asn1_br_read_bytes(reader, " + var + ".data, " + var + ".bit_length);\n";
        }
        code += "    return 0;\n";
    }
    return code;
}

// ── OCTET STRING ──────────────────────────────────────────────────────────

std::string CCodecEmitter::generateOctetStringLogic(const AsnNodePtr& type_node,
                                                      const std::string& var,
                                                      bool is_enc,
                                                      const std::string& prefix) {
    int64_t min_s = 0, max_s = INT64_MAX;
    bool constrained = false;
    if (symbol_table) {
        auto typeInfo = ConstraintResolver::resolveConstraints(type_node, *symbol_table, current_module);
        if (typeInfo && typeInfo->minValue.has_value() && typeInfo->maxValue.has_value()) {
            min_s = (int64_t)typeInfo->minValue.value();
            max_s = (int64_t)typeInfo->maxValue.value();
            constrained = true;
        }
    }

    auto sv = [](int64_t v){ return std::to_string((long long)v) + "LL"; };
    std::string code = "    int rc = 0; (void)rc;\n";
    std::string loop_enc = "    { size_t _i; for (_i = 0; _i < " + var + ".length; ++_i) asn1_bw_write_byte(writer, " + var + ".data[_i]); }\n";
    std::string loop_dec = "    { size_t _i; for (_i = 0; _i < " + var + ".length; ++_i) " + var + ".data[_i] = asn1_br_read_byte(reader); }\n";

    if (is_enc) {
        if (constrained && min_s == max_s) {
            code += "    if (" + var + ".length != (size_t)" + sv(min_s) + ") {\n";
            code += "        char _msg[128];\n";
            code += "        snprintf(_msg, sizeof(_msg), \"OCTET STRING SIZE constraint violation: length %zu != "
                    + std::to_string((long long)min_s) + ".\", " + var + ".length);\n";
            code += "        if (err_buf && err_buf_len > 0) snprintf(err_buf, err_buf_len, \"%s\", _msg);\n";
            code += "        return -1;\n    }\n";
            code += loop_enc;
        } else if (constrained) {
            code += "    if (" + var + ".length < (size_t)" + sv(min_s) + " || " + var + ".length > (size_t)" + sv(max_s) + ") {\n";
            code += "        char _msg[128];\n";
            code += "        snprintf(_msg, sizeof(_msg), \"OCTET STRING SIZE constraint violation: length %zu out of range ["
                    + std::to_string((long long)min_s) + ", " + std::to_string((long long)max_s)
                    + "].\", " + var + ".length);\n";
            code += "        if (err_buf && err_buf_len > 0) snprintf(err_buf, err_buf_len, \"%s\", _msg);\n";
            code += "        return -1;\n    }\n";
            code += "    asn1_uper_encode_length(writer, " + var + ".length, (size_t)" + sv(min_s) + ", (size_t)" + sv(max_s) + ");\n";
            code += loop_enc;
        } else {
            code += "    asn1_uper_encode_unconstrained_length(writer, " + var + ".length);\n";
            code += loop_enc;
        }
        code += "    return 0;\n";
    } else {
        if (constrained && min_s == max_s) {
            code += "    " + var + ".length = (size_t)" + sv(min_s) + ";\n";
            code += "    " + var + ".data = (uint8_t*)malloc((size_t)" + sv(min_s) + ");\n";
            code += "    if (!" + var + ".data) return -1;\n";
            code += loop_dec;
        } else if (constrained) {
            code += "    " + var + ".length = asn1_uper_decode_length(reader, (size_t)" + sv(min_s) + ", (size_t)" + sv(max_s) + ");\n";
            code += "    " + var + ".data = (uint8_t*)malloc(" + var + ".length);\n";
            code += "    if (!" + var + ".data) return -1;\n";
            code += loop_dec;
        } else {
            code += "    " + var + ".length = asn1_uper_decode_unconstrained_length(reader);\n";
            code += "    " + var + ".data = (uint8_t*)malloc(" + var + ".length);\n";
            code += "    if (!" + var + ".data) return -1;\n";
            code += loop_dec;
        }
        code += "    return 0;\n";
    }
    return code;
}

// ── Character strings ─────────────────────────────────────────────────────

std::string CCodecEmitter::generateCharStringLogic(const AsnNodePtr& type_node,
                                                     const std::string& var,
                                                     bool is_enc,
                                                     const std::string& prefix) {
    // Treat char strings like OCTET STRING (UTF-8 bytes)
    return generateOctetStringLogic(type_node, var, is_enc, prefix);
}

// ── ENUMERATED ────────────────────────────────────────────────────────────

std::string CCodecEmitter::generateEnumeratedLogic(const AsnNodePtr& type_node,
                                                     const std::string& var,
                                                     bool is_enc) {
    // Count non-extension enumerators
    int count = 0;
    bool has_ext = false;
    for (size_t i = 0; i < type_node->getChildCount(); ++i) {
        auto e = type_node->getChild(i);
        if (!e) continue;
        if (e->type == NodeType::EXTENSION_MARKER) { has_ext = true; continue; }
        count++;
    }

    std::string code = "    int rc = 0; (void)rc;\n";
    code += "    (void)err_buf; (void)err_buf_len;\n";

    if (is_enc) {
        if (has_ext) {
            code += "    asn1_uper_encode_ext_choice_index(writer, (int)" + var + ", " + std::to_string(count) + ");\n";
        } else {
            int bits = rangeBitsFor(0, count - 1);
            code += "    asn1_bw_write_bits(writer, (uint64_t)(int)" + var + ", " + std::to_string(bits) + ");\n";
        }
        code += "    return 0;\n";
    } else {
        if (has_ext) {
            code += "    { int _idx = asn1_uper_decode_ext_choice_index(reader, " + std::to_string(count) + ");\n";
            code += "      " + var + " = (int)_idx; }\n";
        } else {
            int bits = rangeBitsFor(0, count - 1);
            code += "    " + var + " = (int)asn1_br_read_bits(reader, " + std::to_string(bits) + ");\n";
        }
        code += "    return 0;\n";
    }
    return code;
}

// ── SEQUENCE ──────────────────────────────────────────────────────────────

std::string CCodecEmitter::generateSequenceLogic(const AsnNodePtr& node,
                                                   const std::string& module,
                                                   const std::string& prefix,
                                                   bool is_enc) {
    auto type_node = resolveEffectiveType(node);
    if (!type_node ||
        (type_node->type != NodeType::SEQUENCE && type_node->type != NodeType::SET))
        type_node = node->getChild(0);
    if (!type_node) return "    return 0;\n";

    bool has_ext = type_node->hasExtension;

    // Collect members
    struct Member {
        std::string name;
        bool optional;
        bool in_extension;
        AsnNodePtr member_node;
    };
    std::vector<Member> members;
    bool in_ext = false;
    for (size_t i = 0; i < type_node->getChildCount(); ++i) {
        auto m = type_node->getChild(i);
        if (!m) continue;
        if (m->type == NodeType::EXTENSION_MARKER) { in_ext = true; continue; }
        if (m->type != NodeType::ASSIGNMENT) continue;
        members.push_back({safeId(m->name),
                           m->isOptional || m->hasDefault || in_ext,
                           in_ext, m});
    }

    int opt_count = 0;
    for (const auto& m : members) if (m.optional && !m.in_extension) opt_count++;

    std::string code = "    int rc = 0; (void)rc;\n";

    if (is_enc) {
        if (has_ext) code += "    asn1_uper_encode_ext_marker(writer, 0);\n";

        // Preamble bitmap
        if (opt_count > 0) {
            code += "    {\n";
            code += "        uint64_t _bm = 0;\n";
            int bit = opt_count - 1;
            for (const auto& m : members) {
                if (!m.optional || m.in_extension) continue;
                code += "        if (value->" + m.name + " != 0 || value->has_" + m.name + ") _bm |= (1ull << " + std::to_string(bit--) + ");\n";
            }
            // Rewrite: use has_ flag properly
            code = "    int rc = 0; (void)rc;\n";
            if (has_ext) code += "    asn1_uper_encode_ext_marker(writer, 0);\n";
            code += "    {\n";
            code += "        uint64_t _bm = 0;\n";
            int bbit = opt_count - 1;
            for (const auto& m : members) {
                if (!m.optional || m.in_extension) continue;
                code += "        if (value->has_" + m.name + ") _bm |= (1ull << " + std::to_string(bbit--) + ");\n";
            }
            code += "        asn1_uper_encode_seq_preamble(writer, _bm, " + std::to_string(opt_count) + ");\n";
            code += "    }\n";
        }

        // Encode each non-extension member
        for (const auto& m : members) {
            if (m.in_extension) continue;
            std::string field = "value->" + m.name;

            // Determine type of this member
            auto eff_type = resolveEffectiveType(m.member_node);
            if (!eff_type) eff_type = m.member_node->getChild(0);

            if (m.optional) {
                code += "    if (value->has_" + m.name + ") {\n";
            }

            if (m.member_node->resolvedName.has_value()) {
                std::string ref = cRef(m.member_node->resolvedName.value(), module);
                // Split ref into module_prefix and type_name
                auto last_under = ref.rfind('_');
                std::string ref_mod = (last_under != std::string::npos)
                    ? ref.substr(0, last_under) : ref;
                std::string ref_type = (last_under != std::string::npos)
                    ? ref.substr(last_under + 1) : ref;
                bool scalar = (eff_type &&
                    (eff_type->type == NodeType::INTEGER ||
                     eff_type->type == NodeType::BOOLEAN ||
                     eff_type->type == NodeType::ENUMERATION));
                std::string fn = ref + "_encode_" + ref_type;
                // Use full resolved name as function prefix
                std::string enc_fn = ref + "_encode_" + ref_type;
                // Simplification: call cName(module_of_ref, "encode_" + type_of_ref)
                // The resolved name is "Module.TypeName" → cName
                const auto& res = m.member_node->resolvedName.value();
                auto dot = res.find('.');
                if (dot != std::string::npos) {
                    std::string rmod = res.substr(0, dot);
                    std::string rtype = res.substr(dot + 1);
                    enc_fn = cName(rmod, "encode_" + rtype);
                    std::string arg = scalar ? field : "&(" + field + ")";
                    std::string _pre = m.optional ? std::string("    ") : std::string("");
                    code += "    " + _pre + "rc = " + enc_fn + "(writer, " + arg + ", err_buf, err_buf_len);\n";
                    code += "    " + _pre + "if (rc != 0) return rc;\n";
                } else {
                    code += "    /* unresolved ref: " + m.name + " */\n";
                }
            } else if (eff_type) {
                std::string inner_code;
                auto child0 = m.member_node->getChildCount() > 0 ? m.member_node->getChild(0) : nullptr;
                switch (eff_type->type) {
                    case NodeType::INTEGER:
                        inner_code = generateIntegerLogic(eff_type, field, true, "int64_t");
                        break;
                    case NodeType::BOOLEAN:
                        inner_code = generateBooleanLogic(field, true);
                        break;
                    case NodeType::BIT_STRING:
                        inner_code = generateBitStringLogic(eff_type, field, true, prefix);
                        break;
                    case NodeType::OCTET_STRING:
                    case NodeType::UTF8_STRING:
                    case NodeType::PRINTABLE_STRING:
                    case NodeType::VISIBLE_STRING:
                    case NodeType::IA5_STRING:
                    case NodeType::NUMERIC_STRING:
                        inner_code = generateOctetStringLogic(eff_type, field, true, prefix);
                        break;
                    case NodeType::ENUMERATION:
                        inner_code = generateEnumeratedLogic(eff_type, field, true);
                        break;
                    case NodeType::SEQUENCE:
                    case NodeType::SET:
                    case NodeType::CHOICE:
                    case NodeType::SEQUENCE_OF:
                    case NodeType::SET_OF:
                        if (child0 && child0->resolvedName.has_value()) {
                            const auto& res2 = child0->resolvedName.value();
                            if (!isParameterizedRef(symbol_table, res2)) {
                                auto dot2 = res2.find('.');
                                if (dot2 != std::string::npos) {
                                    std::string rmod2  = res2.substr(0, dot2);
                                    std::string rtype2 = res2.substr(dot2 + 1);
                                    std::string fn2 = cName(rmod2, "encode_" + rtype2);
                                    inner_code  = "    int rc = 0; (void)rc;\n";
                                    inner_code += "    rc = " + fn2 + "(writer, &(" + field + "), err_buf, err_buf_len);\n";
                                    inner_code += "    if (rc != 0) return rc;\n";
                                    inner_code += "    return 0;\n";
                                }
                            }
                        }
                        if (inner_code.empty()) inner_code = "    /* TODO: encode " + m.name + " */\n    return 0;\n";
                        break;
                    default:
                        inner_code = "    /* TODO: encode " + m.name + " */\n    return 0;\n";
                        break;
                }
                // Strip leading "    int rc = 0;\n" to avoid re-declaring rc
                std::string stripped = inner_code;
                std::string rc_decl = "    int rc = 0; (void)rc;\n";
                auto pos = stripped.find(rc_decl);
                if (pos == 0) stripped = stripped.substr(rc_decl.size());
                rc_decl = "    int rc = 0;\n";
                pos = stripped.find(rc_decl);
                if (pos == 0) stripped = stripped.substr(rc_decl.size());
                // Strip trailing "    return 0;\n"
                std::string ret0 = "    return 0;\n";
                if (stripped.size() >= ret0.size() &&
                    stripped.substr(stripped.size() - ret0.size()) == ret0)
                    stripped = stripped.substr(0, stripped.size() - ret0.size());
                if (m.optional) {
                    // Indent by 4 more
                    std::string ind_code;
                    std::istringstream ss(stripped);
                    std::string line;
                    while (std::getline(ss, line)) ind_code += "    " + line + "\n";
                    code += ind_code;
                } else {
                    code += stripped;
                }
            }

            if (m.optional) code += "    }\n";
        }
        code += "    return 0;\n";

    } else {
        // DECODER
        if (has_ext) code += "    int _has_ext = asn1_uper_decode_ext_marker(reader);\n";

        if (opt_count > 0) {
            code += "    uint64_t _preamble = asn1_uper_decode_seq_preamble(reader, " +
                    std::to_string(opt_count) + ");\n";
            int bit = opt_count - 1;
            for (const auto& m : members) {
                if (!m.optional || m.in_extension) continue;
                code += "    out->has_" + m.name + " = (int)((_preamble >> " +
                        std::to_string(bit--) + ") & 1u);\n";
            }
        }

        for (const auto& m : members) {
            if (m.in_extension) continue;
            std::string field = "out->" + m.name;

            auto eff_type = resolveEffectiveType(m.member_node);
            if (!eff_type) eff_type = m.member_node->getChild(0);

            if (m.optional) code += "    if (out->has_" + m.name + ") {\n";

            if (m.member_node->resolvedName.has_value()) {
                const auto& res = m.member_node->resolvedName.value();
                auto dot = res.find('.');
                if (dot != std::string::npos) {
                    std::string rmod  = res.substr(0, dot);
                    std::string rtype = res.substr(dot + 1);
                    std::string dec_fn = cName(rmod, "decode_" + rtype);
                    std::string arg = "&(" + field + ")";
                    std::string _pre2 = m.optional ? std::string("    ") : std::string("");
                    code += _pre2 + "    rc = " + dec_fn + "(reader, " + arg + ", err_buf, err_buf_len);\n";
                    code += _pre2 + "    if (rc != 0) return rc;\n";
                }
            } else if (eff_type) {
                std::string inner_code;
                auto child0 = m.member_node->getChildCount() > 0 ? m.member_node->getChild(0) : nullptr;
                switch (eff_type->type) {
                    case NodeType::INTEGER:
                        inner_code = generateIntegerLogic(eff_type, field, false, "int64_t");
                        break;
                    case NodeType::BOOLEAN:
                        inner_code = generateBooleanLogic(field, false);
                        break;
                    case NodeType::BIT_STRING:
                        inner_code = generateBitStringLogic(eff_type, field, false, prefix);
                        break;
                    case NodeType::OCTET_STRING:
                    case NodeType::UTF8_STRING:
                    case NodeType::PRINTABLE_STRING:
                    case NodeType::VISIBLE_STRING:
                    case NodeType::IA5_STRING:
                    case NodeType::NUMERIC_STRING:
                        inner_code = generateOctetStringLogic(eff_type, field, false, prefix);
                        break;
                    case NodeType::ENUMERATION:
                        inner_code = generateEnumeratedLogic(eff_type, field, false);
                        break;
                    case NodeType::SEQUENCE:
                    case NodeType::SET:
                    case NodeType::CHOICE:
                    case NodeType::SEQUENCE_OF:
                    case NodeType::SET_OF:
                        if (child0 && child0->resolvedName.has_value()) {
                            const auto& res2 = child0->resolvedName.value();
                            if (!isParameterizedRef(symbol_table, res2)) {
                                auto dot2 = res2.find('.');
                                if (dot2 != std::string::npos) {
                                    std::string rmod2  = res2.substr(0, dot2);
                                    std::string rtype2 = res2.substr(dot2 + 1);
                                    std::string fn2 = cName(rmod2, "decode_" + rtype2);
                                    inner_code  = "    int rc = 0; (void)rc;\n";
                                    inner_code += "    rc = " + fn2 + "(reader, &(" + field + "), err_buf, err_buf_len);\n";
                                    inner_code += "    if (rc != 0) return rc;\n";
                                    inner_code += "    return 0;\n";
                                }
                            }
                        }
                        if (inner_code.empty()) inner_code = "    /* TODO: decode " + m.name + " */\n    return 0;\n";
                        break;
                    default:
                        inner_code = "    /* TODO: decode " + m.name + " */\n    return 0;\n";
                        break;
                }
                std::string stripped = inner_code;
                for (const auto& decl : {"    int rc = 0; (void)rc;\n", "    int rc = 0;\n"}) {
                    if (stripped.substr(0, strlen(decl)) == decl)
                        stripped = stripped.substr(strlen(decl));
                }
                std::string ret0 = "    return 0;\n";
                if (stripped.size() >= ret0.size() &&
                    stripped.substr(stripped.size() - ret0.size()) == ret0)
                    stripped = stripped.substr(0, stripped.size() - ret0.size());
                if (m.optional) {
                    std::string ind;
                    std::istringstream ss(stripped);
                    std::string line;
                    while (std::getline(ss, line)) ind += "    " + line + "\n";
                    code += ind;
                } else {
                    code += stripped;
                }
            }

            if (m.optional) code += "    }\n";
        }

        if (has_ext) {
            code += "    if (_has_ext) {\n";
            code += "        /* skip unknown extension additions */\n";
            code += "        out->_ext_data = NULL; out->_ext_len = 0;\n";
            code += "    }\n";
        }
        code += "    return 0;\n";
    }

    return code;
}

// ── CHOICE ────────────────────────────────────────────────────────────────

std::string CCodecEmitter::generateChoiceLogic(const AsnNodePtr& node,
                                                 const std::string& module,
                                                 const std::string& prefix,
                                                 bool is_enc) {
    auto type_node = resolveEffectiveType(node);
    if (!type_node || type_node->type != NodeType::CHOICE)
        type_node = node->getChild(0);
    if (!type_node) return "    return 0;\n";

    bool has_ext = type_node->hasExtension;
    std::string tag_enum = prefix + "_TAG";

    struct Alt { std::string name; AsnNodePtr alt_node; int idx; };
    std::vector<Alt> alts;
    int base_count = 0;
    for (size_t i = 0; i < type_node->getChildCount(); ++i) {
        auto opt = type_node->getChild(i);
        if (!opt || opt->type == NodeType::EXTENSION_MARKER) { has_ext = true; continue; }
        if (opt->type != NodeType::ASSIGNMENT) continue;
        alts.push_back({safeId(opt->name), opt, base_count++});
    }

    std::string code = "    int rc = 0; (void)rc;\n";

    if (is_enc) {
        if (has_ext) {
            code += "    asn1_uper_encode_ext_choice_index(writer, (int)value->tag, " +
                    std::to_string(base_count) + ");\n";
        } else {
            code += "    asn1_uper_encode_choice_index(writer, (int)value->tag, " +
                    std::to_string(base_count) + ");\n";
        }
        code += "    switch ((int)value->tag) {\n";
        for (const auto& a : alts) {
            code += "    case " + std::to_string(a.idx) + ": {\n";
            std::string field = "value->u." + a.name;
            if (a.alt_node->resolvedName.has_value()) {
                const auto& res = a.alt_node->resolvedName.value();
                auto dot = res.find('.');
                if (dot != std::string::npos) {
                    std::string rmod  = res.substr(0, dot);
                    std::string rtype = res.substr(dot + 1);
                    auto eff = resolveEffectiveType(a.alt_node);
                    bool scalar = eff && (eff->type == NodeType::INTEGER ||
                                         eff->type == NodeType::BOOLEAN ||
                                         eff->type == NodeType::ENUMERATION);
                    std::string fn = cName(rmod, "encode_" + rtype);
                    std::string arg = scalar ? field : "&(" + field + ")";
                    code += "        rc = " + fn + "(writer, " + arg + ", err_buf, err_buf_len);\n";
                    code += "        if (rc != 0) return rc;\n";
                }
            } else {
                auto eff = resolveEffectiveType(a.alt_node);
                if (eff) {
                    std::string inner;
                    auto child0 = a.alt_node->getChildCount() > 0 ? a.alt_node->getChild(0) : nullptr;
                    switch (eff->type) {
                        case NodeType::INTEGER:
                            inner = generateIntegerLogic(eff, field, true, "int64_t"); break;
                        case NodeType::BIT_STRING:
                            inner = generateBitStringLogic(eff, field, true, prefix); break;
                        case NodeType::OCTET_STRING:
                        case NodeType::UTF8_STRING:
                        case NodeType::PRINTABLE_STRING:
                        case NodeType::VISIBLE_STRING:
                        case NodeType::IA5_STRING:
                        case NodeType::NUMERIC_STRING:
                            inner = generateOctetStringLogic(eff, field, true, prefix); break;
                        case NodeType::BOOLEAN:
                            inner = generateBooleanLogic(field, true); break;
                        case NodeType::ENUMERATION:
                            inner = generateEnumeratedLogic(eff, field, true); break;
                        case NodeType::SEQUENCE:
                        case NodeType::SET:
                        case NodeType::CHOICE:
                        case NodeType::SEQUENCE_OF:
                        case NodeType::SET_OF:
                            if (child0 && child0->resolvedName.has_value()) {
                                const auto& res2 = child0->resolvedName.value();
                                if (!isParameterizedRef(symbol_table, res2)) {
                                    auto dot2 = res2.find('.');
                                    if (dot2 != std::string::npos) {
                                        std::string rmod2  = res2.substr(0, dot2);
                                        std::string rtype2 = res2.substr(dot2 + 1);
                                        std::string fn2 = cName(rmod2, "encode_" + rtype2);
                                        inner  = "    int rc = 0; (void)rc;\n";
                                        inner += "    rc = " + fn2 + "(writer, &(" + field + "), err_buf, err_buf_len);\n";
                                        inner += "    if (rc != 0) return rc;\n";
                                        inner += "    return 0;\n";
                                    }
                                }
                            }
                            if (inner.empty()) inner = "        /* TODO: compound type " + a.name + " */\n";
                            break;
                        default: inner = "        /* TODO */\n"; break;
                    }
                    // Strip rc decl and return
                    for (const auto& d : {"    int rc = 0; (void)rc;\n","    int rc = 0;\n"})
                        if (inner.substr(0,strlen(d))==d) inner=inner.substr(strlen(d));
                    std::string r="    return 0;\n";
                    if (inner.size()>=r.size() && inner.substr(inner.size()-r.size())==r)
                        inner=inner.substr(0,inner.size()-r.size());
                    // Indent
                    std::string ind; std::istringstream ss(inner); std::string line;
                    while(std::getline(ss,line)) ind+="        "+line+"\n";
                    code += ind;
                }
            }
            code += "        break;\n";
            code += "    }\n";
        }
        code += "    default: break;\n    }\n";
        code += "    return 0;\n";
    } else {
        // DECODER
        if (has_ext) {
            code += "    int _idx = asn1_uper_decode_ext_choice_index(reader, " +
                    std::to_string(base_count) + ");\n";
        } else {
            code += "    int _idx = asn1_uper_decode_choice_index(reader, " +
                    std::to_string(base_count) + ");\n";
        }
        code += "    out->tag = (" + tag_enum + ")_idx;\n";
        code += "    switch (_idx) {\n";
        for (const auto& a : alts) {
            code += "    case " + std::to_string(a.idx) + ": {\n";
            std::string field = "out->u." + a.name;
            if (a.alt_node->resolvedName.has_value()) {
                const auto& res = a.alt_node->resolvedName.value();
                auto dot = res.find('.');
                if (dot != std::string::npos) {
                    std::string rmod  = res.substr(0, dot);
                    std::string rtype = res.substr(dot + 1);
                    std::string fn = cName(rmod, "decode_" + rtype);
                    code += "        rc = " + fn + "(reader, &(" + field + "), err_buf, err_buf_len);\n";
                    code += "        if (rc != 0) return rc;\n";
                }
            } else {
                auto eff = resolveEffectiveType(a.alt_node);
                if (eff) {
                    std::string inner;
                    auto child0 = a.alt_node->getChildCount() > 0 ? a.alt_node->getChild(0) : nullptr;
                    switch (eff->type) {
                        case NodeType::INTEGER:
                            inner = generateIntegerLogic(eff, field, false, "int64_t"); break;
                        case NodeType::BIT_STRING:
                            inner = generateBitStringLogic(eff, field, false, prefix); break;
                        case NodeType::OCTET_STRING:
                        case NodeType::UTF8_STRING:
                        case NodeType::PRINTABLE_STRING:
                        case NodeType::VISIBLE_STRING:
                        case NodeType::IA5_STRING:
                        case NodeType::NUMERIC_STRING:
                            inner = generateOctetStringLogic(eff, field, false, prefix); break;
                        case NodeType::BOOLEAN:
                            inner = generateBooleanLogic(field, false); break;
                        case NodeType::ENUMERATION:
                            inner = generateEnumeratedLogic(eff, field, false); break;
                        case NodeType::SEQUENCE:
                        case NodeType::SET:
                        case NodeType::CHOICE:
                        case NodeType::SEQUENCE_OF:
                        case NodeType::SET_OF:
                            if (child0 && child0->resolvedName.has_value()) {
                                const auto& res2 = child0->resolvedName.value();
                                if (!isParameterizedRef(symbol_table, res2)) {
                                    auto dot2 = res2.find('.');
                                    if (dot2 != std::string::npos) {
                                        std::string rmod2  = res2.substr(0, dot2);
                                        std::string rtype2 = res2.substr(dot2 + 1);
                                        std::string fn2 = cName(rmod2, "decode_" + rtype2);
                                        inner  = "    int rc = 0; (void)rc;\n";
                                        inner += "    rc = " + fn2 + "(reader, &(" + field + "), err_buf, err_buf_len);\n";
                                        inner += "    if (rc != 0) return rc;\n";
                                        inner += "    return 0;\n";
                                    }
                                }
                            }
                            if (inner.empty()) inner = "        /* TODO: compound type " + a.name + " */\n";
                            break;
                        default: inner = "        /* TODO */\n"; break;
                    }
                    for (const auto& d : {"    int rc = 0; (void)rc;\n","    int rc = 0;\n"})
                        if (inner.substr(0,strlen(d))==d) inner=inner.substr(strlen(d));
                    std::string r="    return 0;\n";
                    if (inner.size()>=r.size() && inner.substr(inner.size()-r.size())==r)
                        inner=inner.substr(0,inner.size()-r.size());
                    std::string ind; std::istringstream ss(inner); std::string line;
                    while(std::getline(ss,line)) ind+="        "+line+"\n";
                    code += ind;
                }
            }
            code += "        break;\n    }\n";
        }
        if (has_ext) {
            code += "    default:\n";
            code += "        out->tag = (" + tag_enum + ")(-1);\n";
            code += "        asn1_uper_skip_open_type(reader);\n";
            code += "        break;\n";
        }
        code += "    }\n    return 0;\n";
    }
    return code;
}

// ── SEQUENCE OF ───────────────────────────────────────────────────────────

std::string CCodecEmitter::generateSequenceOfLogic(const AsnNodePtr& node,
                                                     const std::string& module,
                                                     const std::string& prefix,
                                                     bool is_enc) {
    auto type_node = resolveEffectiveType(node);
    if (!type_node ||
        (type_node->type != NodeType::SEQUENCE_OF && type_node->type != NodeType::SET_OF))
        type_node = node->getChild(0);
    if (!type_node) return "    return 0;\n";

    // Extract SIZE constraint
    int64_t min_s = 0, max_s = INT64_MAX;
    bool constrained = false;
    if (symbol_table) {
        auto typeInfo = ConstraintResolver::resolveConstraints(type_node, *symbol_table, current_module);
        if (typeInfo && typeInfo->minValue.has_value() && typeInfo->maxValue.has_value()) {
            min_s = (int64_t)typeInfo->minValue.value();
            max_s = (int64_t)typeInfo->maxValue.value();
            constrained = true;
        }
    }

    // Determine element type info
    std::string elem_ref;
    bool elem_scalar = false;
    AsnNodePtr elem_node = type_node->getChildCount() > 0
        ? type_node->getChild(0) : nullptr;
    if (elem_node && elem_node->resolvedName.has_value()) {
        elem_ref = cRef(elem_node->resolvedName.value(), module);
        auto eff = resolveEffectiveType(elem_node);
        elem_scalar = eff && (eff->type == NodeType::INTEGER ||
                               eff->type == NodeType::BOOLEAN ||
                               eff->type == NodeType::ENUMERATION);
    }

    auto sv = [](int64_t v){ return std::to_string((long long)v) + "LL"; };
    std::string code = "    int rc = 0; (void)rc;\n";

    if (is_enc) {
        if (constrained) {
            code += "    if (value->count < (size_t)" + sv(min_s) + " || value->count > (size_t)" + sv(max_s) + ") {\n";
            code += "        char _msg[128];\n";
            code += "        snprintf(_msg, sizeof(_msg), \"SEQUENCE OF SIZE constraint violation: size %zu out of range ["
                    + std::to_string((long long)min_s) + ", " + std::to_string((long long)max_s)
                    + "].\", value->count);\n";
            code += "        if (err_buf && err_buf_len > 0) snprintf(err_buf, err_buf_len, \"%s\", _msg);\n";
            code += "        return -1;\n    }\n";
            code += "    asn1_uper_encode_length(writer, value->count, (size_t)" + sv(min_s) + ", (size_t)" + sv(max_s) + ");\n";
        } else {
            code += "    asn1_uper_encode_unconstrained_length(writer, value->count);\n";
        }

        if (!elem_ref.empty()) {
            const auto& res = elem_node->resolvedName.value();
            auto dot = res.find('.');
            if (dot != std::string::npos) {
                std::string rmod  = res.substr(0, dot);
                std::string rtype = res.substr(dot + 1);
                std::string fn = cName(rmod, "encode_" + rtype);
                std::string arg = elem_scalar ? "value->items[_i]" : "&(value->items[_i])";
                code += "    { size_t _i;\n";
                code += "      for (_i = 0; _i < value->count; ++_i) {\n";
                code += "          rc = " + fn + "(writer, " + arg + ", err_buf, err_buf_len);\n";
                code += "          if (rc != 0) return rc;\n";
                code += "      }\n    }\n";
            }
        } else {
            code += "    /* TODO: encode elements */\n";
        }
        code += "    return 0;\n";
    } else {
        if (constrained) {
            code += "    out->count = asn1_uper_decode_length(reader, (size_t)" + sv(min_s) + ", (size_t)" + sv(max_s) + ");\n";
        } else {
            code += "    out->count = asn1_uper_decode_unconstrained_length(reader);\n";
        }

        if (!elem_ref.empty()) {
            const auto& res = elem_node->resolvedName.value();
            auto dot = res.find('.');
            if (dot != std::string::npos) {
                std::string rmod  = res.substr(0, dot);
                std::string rtype = res.substr(dot + 1);
                std::string c_elem = cName(rmod, rtype);
                std::string fn = cName(rmod, "decode_" + rtype);
                code += "    out->items = (" + c_elem + "*)malloc(out->count * sizeof(" + c_elem + "));\n";
                code += "    if (!out->items && out->count > 0) return -1;\n";
                std::string arg = elem_scalar ? "&(out->items[_i])" : "&(out->items[_i])";
                code += "    { size_t _i;\n";
                code += "      for (_i = 0; _i < out->count; ++_i) {\n";
                code += "          rc = " + fn + "(reader, " + arg + ", err_buf, err_buf_len);\n";
                code += "          if (rc != 0) { free(out->items); out->items = NULL; return rc; }\n";
                code += "      }\n    }\n";
            }
        } else {
            code += "    out->items = NULL;\n    /* TODO: decode elements */\n";
        }
        code += "    return 0;\n";
    }
    return code;
}

} // namespace asn1::codegen
