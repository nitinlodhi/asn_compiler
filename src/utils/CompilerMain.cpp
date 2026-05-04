#include <iostream>
#include <sstream>
#include <cstring>
#include <algorithm>
#include <queue>
#include <functional>
#include <unordered_map>
#include <unordered_set>
#include "utils/Logger.h"
#include "utils/FileLoader.h"
#include "utils/TestFramework.h"
#include "frontend/AsnLexer.h"
#include "frontend/AsnParser.h"
#include "codegen/cpp/CppEmitter.h"
#include "codegen/cpp/CodecEmitter.h"
#include "codegen/c/CEmitter.h"
#include "codegen/c/CCodecEmitter.h"
#include "codegen/TypeMap.h"
#include "codegen/cpp/JsonEmitter.h"
#include "frontend/SymbolTable.h"
#include "frontend/ConstraintResolver.h"

using namespace asn1;

void printUsage(const char* programName) {
    std::cout << "Usage: " << programName << " [options] <input.asn1>\n"
              << "Options:\n"
              << "  -o, --output <base>   Output base name (default: output)\n"
              << "                        C++: generates <base>.h and <base>.cpp\n"
              << "                        C  : generates <base>.h and <base>.c\n"
              << "  -I <path>             Add a directory to the search path for imports\n"
              << "  -n, --namespace <ns>  C++ namespace (default: asn1::generated)\n"
              << "  --lang <cpp|c>        Output language (default: cpp)\n"
              << "  --json                Also emit <base>_json.hpp with nlohmann/json adapters\n"
              << "  --schema              Also emit <base>_schema.json with type definitions\n"
              << "  --test                Run validation tests\n"
              << "  -v, --verbose         Enable verbose output\n"
              << "  -h, --help            Show this help message\n";
}

static void assignTags(const frontend::AsnNodePtr& node) {
    if (!node) return;

    if (node->type == frontend::NodeType::SEQUENCE ||
        node->type == frontend::NodeType::SET ||
        node->type == frontend::NodeType::CHOICE)
    {
        int currentTag = 0;
        for (const auto& child : node->children) {
            if (child->type == frontend::NodeType::ASSIGNMENT) {
                child->tag_number = currentTag++;
            }
        }
    }

    for (const auto& child : node->children) assignTags(child);
    for (const auto& param : node->parameters) assignTags(param);
}

// Topologically sort top-level assignments so that types depended upon are
// emitted before the types that use them. Uses Kahn's algorithm; ties broken
// by the original schema order so output is deterministic.
static std::vector<frontend::AsnNodePtr> topoSortAssignments(
    const std::vector<frontend::AsnNodePtr>& assignments)
{
    std::unordered_map<std::string, frontend::AsnNodePtr> nameToNode;
    std::unordered_map<const frontend::AsnNode*, std::string> ptrToName;
    std::unordered_map<std::string, size_t> originalOrder;

    for (size_t i = 0; i < assignments.size(); ++i) {
        const auto& node = assignments[i];
        nameToNode[node->name] = node;
        ptrToName[node.get()] = node->name;
        if (node->getChildCount() > 0)
            ptrToName[node->getChild(0).get()] = node->name;
        originalOrder[node->name] = i;
    }

    // Collect direct type dependencies by walking each assignment's AST.
    // We look at resolvedTypeNode but do NOT recurse into it to avoid cycles.
    std::unordered_map<std::string, std::unordered_set<std::string>> depMap;
    for (const auto& node : assignments)
        depMap[node->name]; // ensure every name has an entry

    std::function<void(const frontend::AsnNodePtr&, const std::string&,
                       std::unordered_set<const frontend::AsnNode*>&)> collect;
    collect = [&](const frontend::AsnNodePtr& node, const std::string& owner,
                  std::unordered_set<const frontend::AsnNode*>& visited) {
        if (!node || visited.count(node.get())) return;
        visited.insert(node.get());

        if (node->resolvedTypeNode) {
            auto it = ptrToName.find(node->resolvedTypeNode.get());
            if (it != ptrToName.end() && it->second != owner)
                depMap[owner].insert(it->second);
            // Do not recurse into resolvedTypeNode itself.
        }
        // Track dependencies via resolvedName (set by resolver for non-parameterized bodies).
        if (node->resolvedName.has_value()) {
            const auto& rn = node->resolvedName.value();
            std::string typePart = rn;
            auto dot = rn.rfind('.');
            if (dot != std::string::npos) typePart = rn.substr(dot + 1);
            if (nameToNode.count(typePart) && typePart != owner)
                depMap[owner].insert(typePart);
        }
        // Track dependencies via bare node name for IDENTIFIER nodes in parameterized bodies
        // where resolvedName is not set (resolver skips parameterized assignment bodies).
        if (node->type == frontend::NodeType::IDENTIFIER && !node->resolvedName.has_value()) {
            if (nameToNode.count(node->name) && node->name != owner)
                depMap[owner].insert(node->name);
        }
        for (const auto& c : node->children)    collect(c, owner, visited);
        for (const auto& p : node->parameters)  collect(p, owner, visited);
    };

    for (const auto& node : assignments) {
        std::unordered_set<const frontend::AsnNode*> visited;
        collect(node, node->name, visited);
    }

    // Kahn's algorithm — priority queue breaks ties by original index.
    std::unordered_map<std::string, int> inDegree;
    std::unordered_map<std::string, std::vector<std::string>> dependents;

    for (const auto& node : assignments)
        inDegree[node->name] = 0;

    for (const auto& [name, deps] : depMap) {
        for (const auto& dep : deps) {
            if (nameToNode.count(dep)) {
                inDegree[name]++;
                dependents[dep].push_back(name);
            }
        }
    }

    using PQEntry = std::pair<size_t, std::string>;
    std::priority_queue<PQEntry, std::vector<PQEntry>, std::greater<PQEntry>> pq;
    for (const auto& [name, deg] : inDegree)
        if (deg == 0) pq.push({originalOrder[name], name});

    std::vector<frontend::AsnNodePtr> result;
    result.reserve(assignments.size());
    while (!pq.empty()) {
        auto [ord, name] = pq.top(); pq.pop();
        result.push_back(nameToNode[name]);
        for (const auto& dep_name : dependents[name])
            if (--inDegree[dep_name] == 0)
                pq.push({originalOrder[dep_name], dep_name});
    }

    // Append any remaining nodes (cyclic types) in original order.
    if (result.size() < assignments.size()) {
        std::unordered_set<std::string> emitted;
        for (const auto& n : result) emitted.insert(n->name);
        for (const auto& n : assignments)
            if (!emitted.count(n->name)) result.push_back(n);
    }
    return result;
}

static std::vector<frontend::AsnNodePtr> topoSortModules(
    const std::vector<frontend::AsnNodePtr>& modules)
{
    std::unordered_map<std::string, size_t> nameToIdx;
    for (size_t i = 0; i < modules.size(); ++i)
        nameToIdx[modules[i]->name] = i;

    // dependents[i] = list of module indices that depend on module i
    std::vector<std::vector<size_t>> dependents(modules.size());
    std::vector<int> inDegree(modules.size(), 0);

    for (size_t i = 0; i < modules.size(); ++i) {
        const auto& mod = modules[i];
        for (size_t j = 0; j < mod->getChildCount(); ++j) {
            auto child = mod->getChild(j);
            if (!child || child->type != frontend::NodeType::IMPORTS) continue;
            for (size_t k = 0; k < child->getChildCount(); ++k) {
                auto fromNode = child->getChild(k);
                auto it = nameToIdx.find(fromNode->name);
                if (it != nameToIdx.end() && it->second != i) {
                    // module i imports from module it->second → it->second must come first
                    dependents[it->second].push_back(i);
                    inDegree[i]++;
                }
            }
        }
    }

    std::queue<size_t> q;
    for (size_t i = 0; i < modules.size(); ++i)
        if (inDegree[i] == 0) q.push(i);

    std::vector<frontend::AsnNodePtr> result;
    while (!q.empty()) {
        size_t idx = q.front(); q.pop();
        result.push_back(modules[idx]);
        for (size_t dep : dependents[idx])
            if (--inDegree[dep] == 0) q.push(dep);
    }

    // Append any cyclic/orphan modules
    if (result.size() < modules.size()) {
        std::unordered_set<std::string> emitted;
        for (const auto& m : result) emitted.insert(m->name);
        for (const auto& m : modules)
            if (!emitted.count(m->name)) result.push_back(m);
    }
    return result;
}

// ── Schema JSON builder ───────────────────────────────────────────────────────

static std::string jsonEscape(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (unsigned char c : s) {
        if (c == '"')  { out += "\\\""; }
        else if (c == '\\') { out += "\\\\"; }
        else if (c == '\n') { out += "\\n"; }
        else if (c == '\r') { out += "\\r"; }
        else if (c == '\t') { out += "\\t"; }
        else out += c;
    }
    return out;
}

static std::string nodeKindStr(frontend::NodeType t) {
    using NT = frontend::NodeType;
    switch (t) {
        case NT::SEQUENCE:       return "SEQUENCE";
        case NT::SET:            return "SET";
        case NT::SEQUENCE_OF:    return "SEQUENCE_OF";
        case NT::SET_OF:         return "SET_OF";
        case NT::CHOICE:         return "CHOICE";
        case NT::ENUMERATION:    return "ENUMERATION";
        case NT::INTEGER:        return "INTEGER";
        case NT::BOOLEAN:        return "BOOLEAN";
        case NT::OCTET_STRING:   return "OCTET_STRING";
        case NT::BIT_STRING:     return "BIT_STRING";
        case NT::UTF8_STRING:
        case NT::PRINTABLE_STRING:
        case NT::VISIBLE_STRING:
        case NT::IA5_STRING:
        case NT::NUMERIC_STRING: return "STRING";
        case NT::NULL_TYPE:      return "NULL";
        case NT::REAL:           return "REAL";
        case NT::OBJECT_IDENTIFIER: return "OID";
        case NT::ANY_TYPE:       return "ANY";
        default:                 return "UNKNOWN";
    }
}

static std::string buildTypeRef(const frontend::AsnNodePtr& typeNode,
                                  const std::string& moduleName,
                                  const std::string& inlineHint = "") {
    if (!typeNode) return "\"\"";
    if (typeNode->resolvedName.has_value()) {
        const auto& rn = typeNode->resolvedName.value();
        auto dot = rn.find('.');
        if (dot != std::string::npos) {
            std::string mod = rn.substr(0, dot);
            std::string nm  = rn.substr(dot + 1);
            std::replace(mod.begin(), mod.end(), '-', '_');
            std::replace(nm.begin(),  nm.end(),  '-', '_');
            std::string cur = moduleName;
            std::replace(cur.begin(), cur.end(), '-', '_');
            if (mod == cur)
                return "\"" + jsonEscape(cur + "::" + nm) + "\"";
            return "\"" + jsonEscape(mod + "::" + nm) + "\"";
        }
        // bare name (primitive / CLASS param)
        return "\"" + jsonEscape(rn) + "\"";
    }
    if (!inlineHint.empty())
        return "\"" + jsonEscape(inlineHint) + "\"";
    auto eff = typeNode->resolvedTypeNode ? typeNode->resolvedTypeNode : typeNode;
    return "\"" + nodeKindStr(eff->type) + "\"";
}

// Build JSON schema for all emitted types.
static std::string buildSchemaJson(
    const std::vector<frontend::AsnNodePtr>& all_asts,
    const std::unordered_map<std::string,
                             std::unordered_set<std::string>>& moduleEmittedTypes)
{
    std::ostringstream out;
    out << "{\n  \"types\": [\n";
    bool firstType = true;

    for (const auto& module_ast : all_asts) {
        if (!module_ast || module_ast->type != frontend::NodeType::MODULE) continue;
        std::string modName = codegen::TypeMap::mangleName(module_ast->name);
        const auto& emitted = moduleEmittedTypes.count(module_ast->name)
                              ? moduleEmittedTypes.at(module_ast->name)
                              : std::unordered_set<std::string>{};

        for (size_t ci = 0; ci < module_ast->getChildCount(); ++ci) {
            auto node = module_ast->getChild(ci);
            if (!node || node->type != frontend::NodeType::ASSIGNMENT ||
                node->getChildCount() == 0) continue;
            if (!emitted.count(node->name)) continue;

            auto typeDef = node->getChild(0);
            if (!typeDef) continue;
            auto eff = typeDef->resolvedTypeNode ? typeDef->resolvedTypeNode : typeDef;

            std::string mangledName = codegen::TypeMap::mangleName(node->name);
            std::string key = modName + "::" + mangledName;
            std::string kind = nodeKindStr(eff->type);
            // For IDENTIFIER references (typedef), pick up the effective kind
            if (eff->type == frontend::NodeType::IDENTIFIER) kind = "ALIAS";

            if (!firstType) out << ",\n";
            firstType = false;
            out << "    {\n";
            out << "      \"key\": \"" << jsonEscape(key) << "\",\n";
            out << "      \"module\": \"" << jsonEscape(modName) << "\",\n";
            out << "      \"name\": \"" << jsonEscape(mangledName) << "\",\n";
            out << "      \"kind\": \"" << kind << "\"";

            if (eff->type == frontend::NodeType::SEQUENCE ||
                eff->type == frontend::NodeType::SET) {
                out << ",\n      \"fields\": [";
                bool firstField = true;
                bool inExt = false;
                for (size_t fi = 0; fi < eff->getChildCount(); ++fi) {
                    auto member = eff->getChild(fi);
                    if (!member) continue;
                    if (member->type == frontend::NodeType::EXTENSION_MARKER) {
                        inExt = true; continue;
                    }
                    if (member->type != frontend::NodeType::ASSIGNMENT) continue;
                    auto mTypeNode = member->getChild(0);
                    if (!mTypeNode) continue;
                    auto mEff = mTypeNode->resolvedTypeNode ? mTypeNode->resolvedTypeNode : mTypeNode;
                    std::string fName = codegen::TypeMap::mangleName(member->name);
                    std::string fKind = nodeKindStr(mEff->type);
                    std::string fRef  = buildTypeRef(mTypeNode, module_ast->name,
                                                     key + "::" + fName + "_type");
                    if (!firstField) out << ",";
                    firstField = false;
                    out << "\n        {";
                    out << "\"name\":\"" << jsonEscape(fName) << "\",";
                    out << "\"json_key\":\"" << jsonEscape(member->name) << "\",";
                    out << "\"type_ref\":" << fRef << ",";
                    out << "\"kind\":\"" << fKind << "\",";
                    out << "\"optional\":" << (member->isOptional || inExt ? "true" : "false");

                    // Embed inline type details so the frontend is self-contained.
                    if (mEff->type == frontend::NodeType::ENUMERATION) {
                        out << ",\"values\":[";
                        bool fv = true;
                        for (size_t vi = 0; vi < mEff->getChildCount(); ++vi) {
                            auto en = mEff->getChild(vi);
                            if (!en || en->type == frontend::NodeType::EXTENSION_MARKER) continue;
                            if (!fv) out << ",";
                            fv = false;
                            out << "\"" << jsonEscape(codegen::TypeMap::mangleName(en->name)) << "\"";
                        }
                        out << "]";
                    } else if (mEff->type == frontend::NodeType::CHOICE) {
                        out << ",\"alternatives\":[";
                        bool fa = true;
                        for (size_t ai = 0; ai < mEff->getChildCount(); ++ai) {
                            auto alt = mEff->getChild(ai);
                            if (!alt || alt->type == frontend::NodeType::EXTENSION_MARKER) continue;
                            if (alt->type != frontend::NodeType::ASSIGNMENT) continue;
                            auto aTN = alt->getChild(0);
                            if (!aTN) continue;
                            auto aEff = aTN->resolvedTypeNode ? aTN->resolvedTypeNode : aTN;
                            std::string aName = codegen::TypeMap::mangleName(alt->name);
                            std::string aKind = nodeKindStr(aEff->type);
                            std::string aRef  = buildTypeRef(aTN, module_ast->name,
                                                              key + "::" + fName + "_type::" + aName + "_type");
                            if (!fa) out << ",";
                            fa = false;
                            out << "{\"name\":\"" << jsonEscape(aName)
                                << "\",\"kind\":\"" << aKind
                                << "\",\"type_ref\":" << aRef << "}";
                        }
                        out << "]";
                    } else if (mEff->type == frontend::NodeType::SEQUENCE_OF ||
                               mEff->type == frontend::NodeType::SET_OF) {
                        if (mEff->getChildCount() > 0) {
                            auto elemNode = mEff->getChild(0);
                            auto elemEff  = elemNode->resolvedTypeNode ? elemNode->resolvedTypeNode : elemNode;
                            std::string eKind = nodeKindStr(elemEff->type);
                            std::string eRef  = buildTypeRef(elemNode, module_ast->name,
                                                              key + "::" + fName + "_element");
                            out << ",\"element_kind\":\"" << eKind
                                << "\",\"element_type\":" << eRef;
                        }
                    }
                    // Emit ASN.1 DEFAULT value so the frontend can pre-fill the input.
                    if (member->hasDefault && member->value.has_value()) {
                        const std::string& dv = member->value.value();
                        if (!dv.empty() && dv.front() == '"') {
                            // String literal stored as "\"text\"" — strip the C++ quotes.
                            std::string stripped = dv.substr(1, dv.size() - 2);
                            out << ",\"default\":\"" << jsonEscape(stripped) << "\"";
                        } else if (member->defaultValueIsIdentifier) {
                            // Enum identifier — emit as JSON string.
                            out << ",\"default\":\"" << jsonEscape(dv) << "\"";
                        } else {
                            // Numeric — emit as JSON number.
                            out << ",\"default\":" << dv;
                        }
                    }
                    out << "}";
                }
                out << "\n      ]";
            } else if (eff->type == frontend::NodeType::CHOICE) {
                out << ",\n      \"alternatives\": [";
                bool firstAlt = true;
                for (size_t ai = 0; ai < eff->getChildCount(); ++ai) {
                    auto alt = eff->getChild(ai);
                    if (!alt || alt->type == frontend::NodeType::EXTENSION_MARKER) continue;
                    if (alt->type != frontend::NodeType::ASSIGNMENT) continue;
                    auto aTypeNode = alt->getChild(0);
                    if (!aTypeNode) continue;
                    auto aEff = aTypeNode->resolvedTypeNode ? aTypeNode->resolvedTypeNode : aTypeNode;
                    std::string aName = codegen::TypeMap::mangleName(alt->name);
                    std::string aKind = nodeKindStr(aEff->type);
                    std::string aRef  = buildTypeRef(aTypeNode, module_ast->name,
                                                     key + "::" + aName + "_type");
                    if (!firstAlt) out << ",";
                    firstAlt = false;
                    out << "\n        {";
                    out << "\"name\":\"" << jsonEscape(aName) << "\",";
                    out << "\"kind\":\"" << aKind << "\",";
                    out << "\"type_ref\":" << aRef;
                    out << "}";
                }
                out << "\n      ]";
            } else if (eff->type == frontend::NodeType::ENUMERATION) {
                out << ",\n      \"values\": [";
                bool firstVal = true;
                for (size_t vi = 0; vi < eff->getChildCount(); ++vi) {
                    auto en = eff->getChild(vi);
                    if (!en || en->type == frontend::NodeType::EXTENSION_MARKER) continue;
                    if (!firstVal) out << ",";
                    firstVal = false;
                    out << "\"" << jsonEscape(codegen::TypeMap::mangleName(en->name)) << "\"";
                }
                out << "]";
            } else if (eff->type == frontend::NodeType::SEQUENCE_OF ||
                       eff->type == frontend::NodeType::SET_OF) {
                if (eff->getChildCount() > 0) {
                    auto elemNode = eff->getChild(0);
                    auto elemEff  = elemNode->resolvedTypeNode ? elemNode->resolvedTypeNode : elemNode;
                    std::string eKind = nodeKindStr(elemEff->type);
                    std::string eRef  = buildTypeRef(elemNode, module_ast->name, key + "_element");
                    out << ",\n      \"element_kind\": \"" << eKind << "\"";
                    out << ",\n      \"element_type\": " << eRef;
                }
            }
            // for aliases/primitives: just kind is enough
            out << "\n    }";
        }
    }
    out << "\n  ]\n}\n";
    return out.str();
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        printUsage(argv[0]);
        return 1;
    }

    utils::Logger::setLogLevel(utils::LogLevel::INFO);

    std::string inputFile;
    std::string outputBaseName = "output";
    std::string outputNamespace = "asn1::generated";
    std::string outputLang = "cpp";
    bool runTests = false;
    bool verbose = false;
    bool emitJson = false;
    bool emitSchema = false;
    std::vector<std::string> includePaths;

    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "-h" || arg == "--help") { printUsage(argv[0]); return 0; }
        if (arg == "-o" || arg == "--output") {
            if (i + 1 < argc) {
                std::string out = argv[++i];
                // Strip any file extension (e.g. if caller passes "foo.h")
                size_t pos = out.rfind('.');
                // Only strip if the dot comes after the last path separator
                size_t slash = out.find_last_of("/\\");
                if (pos != std::string::npos && (slash == std::string::npos || pos > slash))
                    outputBaseName = out.substr(0, pos);
                else
                    outputBaseName = out;
            }
        } else if (arg == "-n" || arg == "--namespace") {
            if (i + 1 < argc) outputNamespace = argv[++i];
        } else if (arg == "--lang") {
            if (i + 1 < argc) outputLang = argv[++i];
        } else if (arg == "-I") {
            if (i + 1 < argc) includePaths.push_back(argv[++i]);
        } else if (arg == "--json") {
            emitJson = true;
        } else if (arg == "--schema") {
            emitSchema = true;
        } else if (arg == "--test") {
            runTests = true;
        } else if (arg == "-v" || arg == "--verbose") {
            verbose = true; (void)verbose;
            utils::Logger::setLogLevel(utils::LogLevel::DEBUG);
        } else if (arg[0] != '-') {
            inputFile = arg;
        }
    }

    if (inputFile.empty()) {
        std::cerr << "Error: No input file specified\n";
        printUsage(argv[0]);
        return 1;
    }

    // Derive header guard from the base name (filename only, no path)
    std::string headerGuardBase = outputBaseName;
    size_t lastSlash = headerGuardBase.find_last_of("/\\");
    if (lastSlash != std::string::npos)
        headerGuardBase = headerGuardBase.substr(lastSlash + 1);
    std::string headerGuard = headerGuardBase;
    std::replace_if(headerGuard.begin(), headerGuard.end(),
                    [](char c){ return !std::isalnum(c); }, '_');
    std::transform(headerGuard.begin(), headerGuard.end(), headerGuard.begin(), ::toupper);
    headerGuard = "ASN1_GENERATED_" + headerGuard + "_H";

    std::string headerOutputFile = outputBaseName + ".h";
    std::string sourceOutputFile = outputBaseName + (outputLang == "c" ? ".c" : ".cpp");

    try {
        utils::Logger::info("Building symbol table...");
        frontend::SymbolTable globalSymbolTable;
        std::vector<std::string> filesToParse;
        std::unordered_set<std::string> parsedFiles;
        std::vector<frontend::AsnNodePtr> all_asts;

        filesToParse.push_back(inputFile);

        while (!filesToParse.empty()) {
            std::string currentFile = filesToParse.back();
            filesToParse.pop_back();
            if (parsedFiles.count(currentFile)) continue;

            utils::Logger::info("Parsing file: " + currentFile);
            auto fileContent = utils::FileLoader::loadFile(currentFile);
            if (!fileContent.has_value()) {
                utils::Logger::warning("Could not load imported file: " + currentFile);
                continue;
            }

            frontend::AsnLexer lexer(fileContent.value(), currentFile);
            auto tokens = lexer.tokenize();
            frontend::AsnParser parser(tokens);
            auto file_asts = parser.parseAll();
            parsedFiles.insert(currentFile);

            for (auto& ast : file_asts) {
                all_asts.push_back(ast);
                for (size_t i = 0; i < ast->getChildCount(); ++i) {
                    auto child = ast->getChild(i);
                    if (!child) continue;
                    if (child->type == frontend::NodeType::IMPORTS) {
                        for (size_t j = 0; j < child->getChildCount(); ++j) {
                            auto fromNode = child->getChild(j);
                            std::string moduleToImport = fromNode->name;
                            auto path = utils::FileLoader::findModuleFile(
                                moduleToImport, currentFile, includePaths);
                            if (path) filesToParse.push_back(*path);
                            else utils::Logger::warning(
                                "Could not find imported module '" + moduleToImport + "'");
                        }
                    } else if (child->type == frontend::NodeType::ASSIGNMENT ||
                               child->type == frontend::NodeType::VALUE_ASSIGNMENT ||
                               child->type == frontend::NodeType::OBJECT_SET_ASSIGNMENT) {
                        globalSymbolTable.addSymbol(ast->name, child->name, child);
                    }
                }
            }
        }

        if (all_asts.empty() || !all_asts.front()) {
            utils::Logger::error("No ASN.1 modules were successfully parsed. Aborting.");
            return 1;
        }

        utils::Logger::info("Assigning canonical tags...");
        for (const auto& ast : all_asts) assignTags(ast);

        utils::Logger::info("Symbol table built. Resolving references...");
        globalSymbolTable.resolveReferences(all_asts);
        utils::Logger::info("Reference resolution complete.");

        all_asts = topoSortModules(all_asts);

        utils::Logger::info("Starting code generation (lang=" + outputLang + ")...");

        std::string generatedHeaderCode;
        std::string generatedSourceCode;

        if (outputLang == "c") {
            // ── C backend ────────────────────────────────────────────────────
            codegen::CEmitter c_emitter;
            codegen::CCodecEmitter c_codec;

            generatedHeaderCode += c_emitter.emitHeaderPreamble(headerGuard);

            // .c source includes the generated header + C runtime
            generatedSourceCode += "#include \"" + headerOutputFile.substr(
                headerOutputFile.find_last_of("/\\") == std::string::npos ? 0
                    : headerOutputFile.find_last_of("/\\") + 1) + "\"\n";
            generatedSourceCode += "#include \"runtime/c/asn1_uper.h\"\n";
            generatedSourceCode += "#include <stdlib.h>\n";
            generatedSourceCode += "#include <string.h>\n";
            generatedSourceCode += "#include <stdio.h>\n\n";

            for (const auto& module_ast : all_asts) {
                if (!module_ast || module_ast->type != frontend::NodeType::MODULE) continue;
                utils::Logger::info("Generating C code for module: " + module_ast->name);

                std::vector<frontend::AsnNodePtr> all_assignments;
                for (size_t i = 0; i < module_ast->getChildCount(); ++i) {
                    auto node = module_ast->getChild(i);
                    if (node && node->type == frontend::NodeType::ASSIGNMENT &&
                        node->getChildCount() > 0)
                        all_assignments.push_back(node);
                }
                auto sorted_assignments = topoSortAssignments(all_assignments);

                c_emitter.setContext(globalSymbolTable, module_ast->name);
                c_codec.setContext(globalSymbolTable, module_ast->name);

                // Type definitions in the header
                std::unordered_set<std::string> cEmittedTypes;
                for (const auto& node : sorted_assignments) {
                    auto typeDefNode = node->getChild(0);
                    if (!typeDefNode) continue;
                    bool didEmit = true;
                    switch (typeDefNode->type) {
                        case frontend::NodeType::SEQUENCE:
                        case frontend::NodeType::SET:
                            generatedHeaderCode += c_emitter.emitStruct(node, module_ast->name);
                            break;
                        case frontend::NodeType::SEQUENCE_OF:
                        case frontend::NodeType::SET_OF:
                            generatedHeaderCode += c_emitter.emitSequenceOf(node, module_ast->name);
                            break;
                        case frontend::NodeType::CHOICE:
                            generatedHeaderCode += c_emitter.emitChoice(node, module_ast->name);
                            break;
                        case frontend::NodeType::ENUMERATION:
                            generatedHeaderCode += c_emitter.emitEnum(node, module_ast->name);
                            break;
                        case frontend::NodeType::INTEGER:
                        case frontend::NodeType::BOOLEAN:
                        case frontend::NodeType::OCTET_STRING:
                        case frontend::NodeType::BIT_STRING:
                        case frontend::NodeType::OBJECT_IDENTIFIER:
                        case frontend::NodeType::REAL:
                        case frontend::NodeType::NULL_TYPE:
                        case frontend::NodeType::UTF8_STRING:
                        case frontend::NodeType::PRINTABLE_STRING:
                        case frontend::NodeType::VISIBLE_STRING:
                        case frontend::NodeType::IA5_STRING:
                        case frontend::NodeType::NUMERIC_STRING:
                        case frontend::NodeType::ANY_TYPE:
                            generatedHeaderCode += c_emitter.emitTypedef(node, module_ast->name);
                            break;
                        default:
                            if (typeDefNode->resolvedName.has_value())
                                generatedHeaderCode += c_emitter.emitTypedef(node, module_ast->name);
                            else {
                                utils::Logger::debug("Skipping C type for: " + node->name);
                                didEmit = false;
                            }
                            break;
                    }
                    if (didEmit) cEmittedTypes.insert(node->name);
                }

                // VALUE_ASSIGNMENT → #define constants
                for (size_t i = 0; i < module_ast->getChildCount(); ++i) {
                    auto node = module_ast->getChild(i);
                    if (node && node->type == frontend::NodeType::VALUE_ASSIGNMENT)
                        generatedHeaderCode += c_emitter.emitValueAssignment(node, module_ast->name);
                }

                // Codec declarations (header) and definitions (source)
                size_t total = sorted_assignments.size();
                for (size_t i = 0; i < total; ++i) {
                    const auto& node = sorted_assignments[i];
                    if (!cEmittedTypes.count(node->name)) continue;
                    utils::Logger::info("  - " + node->name + " (" +
                                        std::to_string(i + 1) + "/" +
                                        std::to_string(total) + ")");
                    generatedHeaderCode += c_codec.emitEncoderDeclaration(node, module_ast->name);
                    generatedHeaderCode += c_codec.emitDecoderDeclaration(node, module_ast->name);
                    generatedSourceCode += c_codec.emitEncoderDefinition(node, module_ast->name);
                    generatedSourceCode += c_codec.emitDecoderDefinition(node, module_ast->name);
                }
            }

            generatedHeaderCode += c_emitter.emitHeaderEpilogue(headerGuard);

        } else {
            // ── C++ backend (default) ─────────────────────────────────────────
            codegen::CppEmitter emitter;
            emitter.setOutputNamespace(outputNamespace);
            codegen::CodecEmitter codec_emitter;
            // Tracks which types were actually emitted per module (for JSON pass filtering).
            std::unordered_map<std::string, std::unordered_set<std::string>> moduleEmittedTypes;

            generatedHeaderCode += emitter.emitHeaderPreamble(headerGuard);
            generatedHeaderCode += "namespace " + outputNamespace + " {\n\n";

            generatedSourceCode += emitter.emitSourcePreamble(headerOutputFile);
            generatedSourceCode += "namespace " + outputNamespace + " {\n\n";

            for (const auto& module_ast : all_asts) {
                if (!module_ast || module_ast->type != frontend::NodeType::MODULE) continue;

                std::string moduleNamespace = codegen::TypeMap::mangleName(module_ast->name);
                utils::Logger::info("Generating C++ code for module: " + module_ast->name);

                std::vector<frontend::AsnNodePtr> all_assignments;
                for (size_t i = 0; i < module_ast->getChildCount(); ++i) {
                    auto node = module_ast->getChild(i);
                    if (node && node->type == frontend::NodeType::ASSIGNMENT &&
                        node->getChildCount() > 0)
                        all_assignments.push_back(node);
                }
                auto sorted_assignments = topoSortAssignments(all_assignments);

                generatedHeaderCode += "namespace " + moduleNamespace + " {\n\n";
                generatedSourceCode += "namespace " + moduleNamespace + " {\n\n";

                codec_emitter.setContext(globalSymbolTable, module_ast->name);

                std::unordered_set<std::string> emittedTypes;
                for (const auto& node : sorted_assignments) {
                    auto typeDefNode = node->getChild(0);
                    if (!typeDefNode) continue;
                    bool didEmit = true;
                    switch (typeDefNode->type) {
                        case frontend::NodeType::SEQUENCE:
                        case frontend::NodeType::SET:
                            generatedHeaderCode += emitter.emitStruct(node, module_ast->name);
                            break;
                        case frontend::NodeType::SEQUENCE_OF:
                        case frontend::NodeType::SET_OF:
                            generatedHeaderCode += emitter.emitSequenceOf(node, module_ast->name);
                            break;
                        case frontend::NodeType::CHOICE:
                            generatedHeaderCode += emitter.emitChoice(node, module_ast->name);
                            break;
                        case frontend::NodeType::ENUMERATION:
                            generatedHeaderCode += emitter.emitEnum(node, module_ast->name);
                            break;
                        case frontend::NodeType::INTEGER:
                        case frontend::NodeType::BOOLEAN:
                        case frontend::NodeType::OCTET_STRING:
                        case frontend::NodeType::BIT_STRING:
                        case frontend::NodeType::OBJECT_IDENTIFIER:
                        case frontend::NodeType::REAL:
                        case frontend::NodeType::NULL_TYPE:
                        case frontend::NodeType::UTF8_STRING:
                        case frontend::NodeType::PRINTABLE_STRING:
                        case frontend::NodeType::VISIBLE_STRING:
                        case frontend::NodeType::IA5_STRING:
                        case frontend::NodeType::NUMERIC_STRING:
                        case frontend::NodeType::ANY_TYPE:
                            generatedHeaderCode += emitter.emitTypedef(node, module_ast->name);
                            break;
                        default: {
                            auto resolvedDef = typeDefNode->resolvedTypeNode
                                               ? typeDefNode->resolvedTypeNode : typeDefNode;
                            if (typeDefNode->resolvedName.has_value()) {
                                generatedHeaderCode += emitter.emitTypedef(node, module_ast->name);
                            } else if (resolvedDef->type == frontend::NodeType::SEQUENCE ||
                                       resolvedDef->type == frontend::NodeType::SET) {
                                generatedHeaderCode += emitter.emitStruct(node, module_ast->name);
                            } else if (resolvedDef->type == frontend::NodeType::SEQUENCE_OF ||
                                       resolvedDef->type == frontend::NodeType::SET_OF) {
                                generatedHeaderCode += emitter.emitSequenceOf(node, module_ast->name);
                            } else if (resolvedDef->type == frontend::NodeType::CHOICE) {
                                generatedHeaderCode += emitter.emitChoice(node, module_ast->name);
                            } else if (resolvedDef->type == frontend::NodeType::ENUMERATION) {
                                generatedHeaderCode += emitter.emitEnum(node, module_ast->name);
                            } else if (typeDefNode->type == frontend::NodeType::IDENTIFIER) {
                                // Parameterized alias (e.g. Foo {params} ::= Bar {params})
                                // Emit as a using alias of the base type name.
                                std::string emitted = emitter.emitTypedef(node, module_ast->name);
                                if (emitted.empty()) { didEmit = false; utils::Logger::debug("Skipping C++ type for: " + node->name); }
                                else generatedHeaderCode += emitted;
                            } else {
                                utils::Logger::debug("Skipping C++ type for: " + node->name);
                                didEmit = false;
                            }
                            break;
                        }
                    }
                    if (didEmit) emittedTypes.insert(node->name);
                }

                for (size_t i = 0; i < module_ast->getChildCount(); ++i) {
                    auto node = module_ast->getChild(i);
                    if (node && node->type == frontend::NodeType::VALUE_ASSIGNMENT)
                        generatedHeaderCode += emitter.emitValueAssignment(node, module_ast->name);
                }

                size_t total = sorted_assignments.size();
                for (size_t i = 0; i < total; ++i) {
                    const auto& node = sorted_assignments[i];
                    if (!emittedTypes.count(node->name)) continue;
                    utils::Logger::info("  - " + node->name + " (" +
                                        std::to_string(i + 1) + "/" +
                                        std::to_string(total) + ")");
                    generatedHeaderCode += codec_emitter.emitEncoderDeclaration(node, module_ast->name);
                    generatedHeaderCode += codec_emitter.emitDecoderDeclaration(node, module_ast->name);
                    generatedSourceCode += codec_emitter.emitEncoderDefinition(node, module_ast->name);
                    generatedSourceCode += codec_emitter.emitDecoderDefinition(node, module_ast->name);
                }

                moduleEmittedTypes[module_ast->name] = emittedTypes;

                generatedHeaderCode += "\n} // namespace " + moduleNamespace + "\n\n";
                generatedSourceCode += "} // namespace " + moduleNamespace + "\n\n";
            }

            generatedHeaderCode += "} // namespace " + outputNamespace + "\n\n#endif // " + headerGuard + "\n";
            generatedSourceCode += "} // namespace " + outputNamespace + "\n";

            // ── Optional: JSON adapter file ──────────────────────────────────
            if (emitJson) {
                std::string headerBasename = headerOutputFile;
                size_t lastSlashJ = headerBasename.find_last_of("/\\");
                if (lastSlashJ != std::string::npos)
                    headerBasename = headerBasename.substr(lastSlashJ + 1);

                codegen::JsonEmitter json_emitter;
                json_emitter.setOutputNamespace(outputNamespace);
                json_emitter.setGeneratedHeader(headerBasename);

                std::string jsonCode = json_emitter.emitPreamble();
                jsonCode += "namespace " + outputNamespace + " {\n\n";

                for (const auto& module_ast : all_asts) {
                    if (!module_ast || module_ast->type != frontend::NodeType::MODULE) continue;
                    std::string moduleNamespace = codegen::TypeMap::mangleName(module_ast->name);

                    // Re-sort assignments (same order as the C++ pass above).
                    std::vector<frontend::AsnNodePtr> all_assignments;
                    for (size_t i = 0; i < module_ast->getChildCount(); ++i) {
                        auto node = module_ast->getChild(i);
                        if (node && node->type == frontend::NodeType::ASSIGNMENT &&
                            node->getChildCount() > 0)
                            all_assignments.push_back(node);
                    }
                    auto sorted_assignments = topoSortAssignments(all_assignments);

                    jsonCode += "namespace " + moduleNamespace + " {\n\n";
                    const auto& emitted = moduleEmittedTypes[module_ast->name];
                    for (const auto& node : sorted_assignments) {
                        if (!emitted.count(node->name)) continue;
                        jsonCode += json_emitter.emitTypeAdapter(node, module_ast->name);
                    }
                    jsonCode += "} // namespace " + moduleNamespace + "\n\n";
                }

                jsonCode += "} // namespace " + outputNamespace + "\n\n";
                jsonCode += json_emitter.emitRegisterFunction();
                jsonCode += "\n";

                std::string jsonOutputFile = outputBaseName + "_json.hpp";
                if (!utils::FileLoader::saveFile(jsonOutputFile, jsonCode)) {
                    utils::Logger::error("Failed to write: " + jsonOutputFile);
                    return 1;
                }
                utils::Logger::info("JSON adapters written to: " + jsonOutputFile);
            }

            // ── Optional: Schema JSON file ───────────────────────────────────
            if (emitSchema) {
                std::string schemaJson = buildSchemaJson(all_asts, moduleEmittedTypes);
                std::string schemaOutputFile = outputBaseName + "_schema.json";
                if (!utils::FileLoader::saveFile(schemaOutputFile, schemaJson)) {
                    utils::Logger::error("Failed to write: " + schemaOutputFile);
                    return 1;
                }
                utils::Logger::info("Type schema written to: " + schemaOutputFile);
            }
        }

        utils::Logger::info("Writing output to: " + headerOutputFile +
                            " and " + sourceOutputFile);
        if (!utils::FileLoader::saveFile(headerOutputFile, generatedHeaderCode)) {
            utils::Logger::error("Failed to write: " + headerOutputFile);
            return 1;
        }
        if (!utils::FileLoader::saveFile(sourceOutputFile, generatedSourceCode)) {
            utils::Logger::error("Failed to write: " + sourceOutputFile);
            return 1;
        }

        utils::Logger::info("Compilation successful!");

        if (runTests) {
            utils::Logger::info("Running validation tests...");
            utils::TestFramework testFramework;
            testFramework.addTest("Basic token test", []() { return true; });
            testFramework.printResults();
        }

        return 0;
    } catch (const std::exception& e) {
        utils::Logger::error("Compilation failed: " + std::string(e.what()));
        return 1;
    }
}
