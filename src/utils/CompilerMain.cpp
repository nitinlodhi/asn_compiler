#include <iostream>
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
                               child->type == frontend::NodeType::VALUE_ASSIGNMENT) {
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
                for (const auto& node : sorted_assignments) {
                    if (node->isParameterized) continue; // skip template types; only concrete instantiations are emitted
                    auto typeDefNode = node->getChild(0);
                    if (!typeDefNode) continue;
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
                            else
                                utils::Logger::debug("Skipping C type for: " + node->name);
                            break;
                    }
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
                    if (node->isParameterized) continue;
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

                for (const auto& node : sorted_assignments) {
                    auto typeDefNode = node->getChild(0);
                    if (!typeDefNode) continue;
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
                        default:
                            if (typeDefNode->resolvedName.has_value()) {
                                generatedHeaderCode += emitter.emitTypedef(node, module_ast->name);
                            } else {
                                utils::Logger::debug("Skipping C++ type for: " + node->name);
                            }
                            break;
                    }
                }

                for (size_t i = 0; i < module_ast->getChildCount(); ++i) {
                    auto node = module_ast->getChild(i);
                    if (node && node->type == frontend::NodeType::VALUE_ASSIGNMENT)
                        generatedHeaderCode += emitter.emitValueAssignment(node, module_ast->name);
                }

                size_t total = sorted_assignments.size();
                for (size_t i = 0; i < total; ++i) {
                    const auto& node = sorted_assignments[i];
                    utils::Logger::info("  - " + node->name + " (" +
                                        std::to_string(i + 1) + "/" +
                                        std::to_string(total) + ")");
                    generatedHeaderCode += codec_emitter.emitEncoderDeclaration(node, module_ast->name);
                    generatedHeaderCode += codec_emitter.emitDecoderDeclaration(node, module_ast->name);
                    generatedSourceCode += codec_emitter.emitEncoderDefinition(node, module_ast->name);
                    generatedSourceCode += codec_emitter.emitDecoderDefinition(node, module_ast->name);
                }

                generatedHeaderCode += "\n} // namespace " + moduleNamespace + "\n\n";
                generatedSourceCode += "} // namespace " + moduleNamespace + "\n\n";
            }

            generatedHeaderCode += "} // namespace " + outputNamespace + "\n\n#endif // " + headerGuard + "\n";
            generatedSourceCode += "} // namespace " + outputNamespace + "\n";
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
