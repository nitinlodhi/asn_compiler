#include "frontend/SymbolTable.h"
#include <unordered_set>
#include <iostream>

namespace asn1::frontend {
static void resolveNodeReferences(AsnNodePtr node, const SymbolTable& table, const std::unordered_map<std::string, std::string>& scope, AsnNodePtr moduleNode, std::unordered_set<std::string>& resolution_path);

SymbolTable::SymbolTable() {}

bool SymbolTable::addSymbol(const std::string& moduleName, const std::string& symbolName, AsnNodePtr node) {
    if (isSymbolDefined(moduleName, symbolName)) {
        return false;
    }
    modules[moduleName][symbolName] = node;
    return true;
}

AsnNodePtr SymbolTable::lookupSymbol(const std::string& moduleName, const std::string& symbolName) const {
    auto mod_it = modules.find(moduleName);
    if (mod_it != modules.end()) {
        auto sym_it = mod_it->second.find(symbolName);
        if (sym_it != mod_it->second.end()) {
            return sym_it->second;
        }
    }
    return nullptr;
}

std::optional<std::pair<std::string, AsnNodePtr>> SymbolTable::findSymbolInAnyModule(const std::string& symbolName) const {
    for (const auto& [modName, syms] : modules) {
        auto sym_it = syms.find(symbolName);
        if (sym_it != syms.end()) {
            return std::make_pair(modName, sym_it->second);
        }
    }
    return std::nullopt;
}

bool SymbolTable::addTypeInfo(const std::string& name, AsnTypeInfoPtr info) {
    if (typeInfo.find(name) != typeInfo.end()) {
        return false;
    }
    typeInfo[name] = info;
    return true;
}

AsnTypeInfoPtr SymbolTable::getTypeInfo(const std::string& name) const {
    auto it = typeInfo.find(name);
    if (it != typeInfo.end()) {
        return it->second;
    }
    return nullptr;
}

static void buildOpenTypeMap(
    AsnNodePtr openTypeNode,
    const std::string& objectSetName,
    const std::string& idFieldName,
    const SymbolTable& table,
    const std::unordered_map<std::string, std::string>& scope,
    AsnNodePtr moduleNode,
    std::unordered_set<std::string>& resolution_path)
{
    auto objectSetAssignment = table.lookupSymbol(moduleNode->name, objectSetName);
    if (!objectSetAssignment || objectSetAssignment->type != NodeType::OBJECT_SET_ASSIGNMENT) {
        // Object set may be unresolvable (e.g., defined in a module not yet parsed, or uses
        // WITH SYNTAX notation that was skipped). Silently skip open-type map building.
        return;
    }

    // Object set must have a class reference child (child 0) and an object set body (child 1)
    auto objectSetClassRef = objectSetAssignment->getChild(0);
    if (!objectSetClassRef) return;
    auto class_it = scope.find(objectSetClassRef->name);
    if (class_it == scope.end()) {
        return;
    }
    std::string qualifiedClassName = class_it->second;
    size_t dotPos = qualifiedClassName.find('.');
    if (dotPos == std::string::npos) return;
    std::string classModuleName = qualifiedClassName.substr(0, dotPos);
    std::string classSymbolName = qualifiedClassName.substr(dotPos + 1);
    auto classAssignment = table.lookupSymbol(classModuleName, classSymbolName);
    if (!classAssignment) return;
    auto classDefNode = classAssignment->getChild(0);
    if (!classDefNode) return;

    std::string typeFieldName;
    for (const auto& fieldSpec : classDefNode->children) {
        if (fieldSpec->isTypeField) {
            typeFieldName = fieldSpec->name;
            break;
        }
    }

    if (typeFieldName.empty()) {
        throw std::runtime_error("Class '" + classSymbolName + "' has no TYPE field for open type resolution.");
    }

    auto objectSetNode = objectSetAssignment->getChild(1);
    if (!objectSetNode) return;
    for (const auto& objectDef : objectSetNode->children) {
        long long id_val = -1;
        AsnNodePtr type_val_node = nullptr;

        for (const auto& fieldAssignment : objectDef->children) {
            if (fieldAssignment->name == idFieldName) {
                auto idChild = fieldAssignment->getChild(0);
                if (idChild && idChild->value.has_value()) {
                    try { id_val = std::stoll(idChild->value.value()); } catch (...) {}
                }
            }
            if (fieldAssignment->name == typeFieldName) {
                type_val_node = fieldAssignment->getChild(0);
            }
        }
        if (id_val != -1 && type_val_node) {
            resolveNodeReferences(type_val_node, table, scope, moduleNode, resolution_path);
            openTypeNode->openTypeMap[id_val] = type_val_node;
        }
    }
}

static void resolveNodeReferences(AsnNodePtr node, const SymbolTable& table, const std::unordered_map<std::string, std::string>& scope, AsnNodePtr moduleNode, std::unordered_set<std::string>& resolution_path) {
    if (!node) return;

    // If this node is a type reference, look it up.
    if (node->type == NodeType::IDENTIFIER) {
        // Already resolved in a prior module pass — do not re-resolve using a
        // different module's scope, which would fail for cross-module references.
        if (node->resolvedName.has_value()) return;
        auto it = scope.find(node->name);
        if (it == scope.end()) {
             // It might be a primitive type, which is okay.
             const std::unordered_set<std::string> primitives = {
                 "INTEGER", "BOOLEAN", "OCTET STRING", "BIT STRING",
                 "NULL", "REAL", "OBJECT IDENTIFIER",
                 "TYPE"   // ASN.1 open-type keyword in CLASS field specs
             };
             if (primitives.find(node->name) != primitives.end()) {
                 // It's a primitive — nothing to do.
             } else {
                 // Not in current module scope. Try a global search across all modules.
                 // This handles cross-module forward references when modules are resolved
                 // in file order before their dependencies.
                 auto globalResult = table.findSymbolInAnyModule(node->name);
                 if (globalResult.has_value()) {
                     node->resolvedName = globalResult->first + "." + node->name;
                     // Don't recurse here — the symbol will be fully resolved in its own module's pass.
                 } else {
                     throw std::runtime_error("Undefined type reference: '" + node->name + "' at " + node->location.toString());
                 }
             }
        } else {
            node->resolvedName = it->second;
            // Also find the node for the resolved type
            std::string qualifiedName = it->second;
            size_t dotPos = qualifiedName.find('.');
            if (dotPos != std::string::npos) {
                std::string moduleName = qualifiedName.substr(0, dotPos);
                std::string symbolName = qualifiedName.substr(dotPos + 1);
                auto assignmentNode = table.lookupSymbol(moduleName, symbolName);
                if (assignmentNode) {
                    if (!assignmentNode->isParameterized) {
                        // Check for circular dependencies on non-parameterized types.
                        if (resolution_path.count(it->second)) {
                            throw std::runtime_error("Circular type reference detected for '" + node->name + "' at " + node->location.toString());
                        }
                        resolution_path.insert(it->second);

                        // Recursively resolve the referenced type itself to detect cycles
                        // and ensure it's fully resolved before we use it.
                        resolveNodeReferences(assignmentNode, table, scope, moduleNode, resolution_path);
                    }

                    if (assignmentNode->isParameterized) {
                        // This is a parameterized type. We need to perform substitution.
                        if (assignmentNode->parameters.size() != node->parameters.size()) {
                            throw std::runtime_error("Mismatched number of parameters for type '" + node->name + "' at " + node->location.toString());
                        }

                        auto parameterizedTypeBody = assignmentNode->getChild(0)->deepCopy();

                        std::unordered_map<std::string, AsnNodePtr> substitutionMap;
                        for (size_t i = 0; i < assignmentNode->parameters.size(); ++i) {
                            auto paramDef = assignmentNode->parameters[i];
                            auto paramUsage = node->parameters[i];
                            resolveNodeReferences(paramUsage, table, scope, moduleNode, resolution_path);
                            substitutionMap[paramDef->name] = paramUsage;
                        }

                        std::vector<AsnNodePtr> nodesToVisit;
                        nodesToVisit.push_back(parameterizedTypeBody);
                        while(!nodesToVisit.empty()) {
                            auto current_node = nodesToVisit.back();
                            nodesToVisit.pop_back();

                            if (current_node->type == NodeType::IDENTIFIER) {
                                auto it = substitutionMap.find(current_node->name);
                                if (it != substitutionMap.end()) {
                                    auto replacementNode = it->second;
                                    // Replace the parameter with the actual type reference
                                    current_node->name = replacementNode->name;
                                    current_node->resolvedName = replacementNode->resolvedName;
                                    current_node->resolvedTypeNode = replacementNode->resolvedTypeNode;
                                    current_node->children = replacementNode->children; 
                                }
                            }

                            for (const auto& child : current_node->children) {
                                nodesToVisit.push_back(child);
                            }
                        }
                        
                        node->resolvedTypeNode = parameterizedTypeBody;
                    } else {
                        node->resolvedTypeNode = assignmentNode->getChild(0);

                        // Apply WITH COMPONENTS constraint: synthesize a filtered SEQUENCE.
                        for (size_t ci = 0; ci < node->getChildCount(); ++ci) {
                            auto constraintChild = node->getChild(ci);
                            if (constraintChild->type == NodeType::CONSTRAINT &&
                                constraintChild->name == "WithComponents" &&
                                node->resolvedTypeNode &&
                                (node->resolvedTypeNode->type == NodeType::SEQUENCE ||
                                 node->resolvedTypeNode->type == NodeType::SET)) {

                                std::unordered_set<std::string> allowed;
                                for (size_t ci2 = 0; ci2 < constraintChild->getChildCount(); ++ci2) {
                                    allowed.insert(constraintChild->getChild(ci2)->name);
                                }

                                auto synthSeq = std::make_shared<AsnNode>(
                                    node->resolvedTypeNode->type, "SEQUENCE", node->resolvedTypeNode->location);
                                synthSeq->hasExtension = constraintChild->hasExtension;

                                for (size_t mi = 0; mi < node->resolvedTypeNode->getChildCount(); ++mi) {
                                    auto member = node->resolvedTypeNode->getChild(mi);
                                    if (!member) continue;
                                    if (member->type == NodeType::EXTENSION_MARKER) {
                                        if (constraintChild->hasExtension)
                                            synthSeq->addChild(member);
                                        continue;
                                    }
                                    if (allowed.count(member->name)) {
                                        synthSeq->addChild(member);
                                    }
                                }
                                node->resolvedTypeNode = synthSeq;
                                break;
                            }
                        }
                    }

                    if (!assignmentNode->isParameterized) {
                        resolution_path.erase(it->second);
                    }
                }
            }
        }
    } else if (node->type == NodeType::FIELD_REFERENCE) {
        if (node->resolvedName.has_value()) return; // already resolved in a prior pass
        // Handles `MY-CLASS.&field` (class field ref) and `mySet.&field` (object set field ref).
        const std::string& lhsName = node->name;
        const std::string& fieldName = node->value.value();

        auto lhs_it = scope.find(lhsName);
        if (lhs_it == scope.end()) {
            throw std::runtime_error("Undefined class reference: '" + lhsName + "' at " + node->location.toString());
        }

        std::string qualifiedLhs = lhs_it->second;
        size_t dotPos = qualifiedLhs.find('.');
        auto lhsAssignment = table.lookupSymbol(qualifiedLhs.substr(0, dotPos), qualifiedLhs.substr(dotPos + 1));
        if (!lhsAssignment) {
            throw std::runtime_error("Symbol '" + lhsName + "' not found at " + node->location.toString());
        }

        // Resolve the actual CLASS_DEFINITION: lhs may be a class or an object set instance.
        AsnNodePtr classDefNode = nullptr;
        if (lhsAssignment->getChild(0) && lhsAssignment->getChild(0)->type == NodeType::CLASS_DEFINITION) {
            classDefNode = lhsAssignment->getChild(0);
        } else if (lhsAssignment->getChild(0) && lhsAssignment->getChild(0)->type == NodeType::IDENTIFIER) {
            // lhsName is an object set; child(0) is the class reference identifier.
            const std::string& classRefName = lhsAssignment->getChild(0)->name;
            auto class_it2 = scope.find(classRefName);
            if (class_it2 != scope.end()) {
                std::string q2 = class_it2->second;
                size_t dp2 = q2.find('.');
                auto classAssign2 = table.lookupSymbol(q2.substr(0, dp2), q2.substr(dp2 + 1));
                if (classAssign2 && classAssign2->getChild(0) &&
                    classAssign2->getChild(0)->type == NodeType::CLASS_DEFINITION) {
                    classDefNode = classAssign2->getChild(0);
                }
            }
        }

        if (!classDefNode) {
            throw std::runtime_error("Symbol '" + lhsName + "' is not an information object class at " + node->location.toString());
        }

        // Find the field within the class definition.
        AsnNodePtr fieldTypeNode = nullptr;
        for (size_t i = 0; i < classDefNode->getChildCount(); ++i) {
            auto fieldSpecNode = classDefNode->getChild(i);
            if (fieldSpecNode->name == fieldName) {
                fieldTypeNode = fieldSpecNode->getChild(0);
                break;
            }
        }

        if (!fieldTypeNode) {
            throw std::runtime_error("Field '&" + fieldName + "' not found in class '" + lhsName + "' at " + node->location.toString());
        }

        node->resolvedTypeNode = fieldTypeNode;

        // Resolve the field's type. Primitives and the open-type marker "TYPE" are not in scope.
        if (fieldTypeNode->type == NodeType::IDENTIFIER) {
            static const std::unordered_set<std::string> fieldPrimitives = {
                "INTEGER", "BOOLEAN", "OCTET STRING", "BIT STRING", "NULL",
                "REAL", "OBJECT IDENTIFIER", "TYPE"
            };
            if (fieldPrimitives.count(fieldTypeNode->name)) {
                node->resolvedName = fieldTypeNode->name;
            } else {
                auto field_type_it = scope.find(fieldTypeNode->name);
                if (field_type_it == scope.end()) {
                    throw std::runtime_error("Undefined type '" + fieldTypeNode->name + "' for field '&" + fieldName + "' at " + fieldTypeNode->location.toString());
                }
                node->resolvedName = field_type_it->second;
            }
        } else {
            node->resolvedName = fieldTypeNode->name;
        }
    } else if (node->type == NodeType::SEQUENCE) {
        // Look for open type fields
        for (const auto& member : node->children) {
            if (member->type != NodeType::ASSIGNMENT || member->children.empty()) continue;

            auto typeNode = member->getChild(0);
            
            // Case 1: ANY DEFINED BY ...
            if (typeNode->type == NodeType::ANY_TYPE && typeNode->value.has_value()) {
                typeNode->definingFieldName = typeNode->value.value();
                AsnNodePtr definingField = nullptr;
                for (const auto& sibling : node->children) { if (sibling->name == *typeNode->definingFieldName) { definingField = sibling; break; } }

                if (definingField) {
                    auto definingFieldType = definingField->getChild(0);
                    AsnNodePtr constraintNode = nullptr;
                    for (const auto& child : definingFieldType->children) { if (child->type == NodeType::CONSTRAINT) { constraintNode = child; break; } }

                    if (constraintNode && constraintNode->name == "TableConstraint") {
                        auto fieldRefNode = constraintNode->getChild(0);
                        const std::string& objectSetName = fieldRefNode->name;
                        const std::string& idFieldName = fieldRefNode->value.value();
                        buildOpenTypeMap(typeNode, objectSetName, idFieldName, table, scope, moduleNode, resolution_path);
                    }
                }
            } 
            // Case 2: Parameterized open type
            else if (typeNode->type == NodeType::FIELD_REFERENCE && !typeNode->parameters.empty()) {
                std::string objectSetName;
                std::string definingFieldName; // Sibling field name, e.g., "id"
                
                for (const auto& param : typeNode->parameters) {
                    if (param->type == NodeType::IDENTIFIER) {
                        objectSetName = param->name;
                    } else if (param->type == NodeType::RELATIVE_REFERENCE) {
                        definingFieldName = param->name;
                    }
                }

                if (!objectSetName.empty() && !definingFieldName.empty()) {
                    typeNode->definingFieldName = definingFieldName;
                    const std::string& idFieldNameInClass = definingFieldName;
                    buildOpenTypeMap(typeNode, objectSetName, idFieldNameInClass, table, scope, moduleNode, resolution_path);
                }
            }
        }
    }

    // If this is a parameterized type definition, don't resolve its body here.
    // The body will be resolved upon instantiation/usage.
    if (node->type == NodeType::ASSIGNMENT && node->isParameterized) {
        return;
    }

    // Recurse on children and parameters
    for (const auto& child : node->children) {
        // The type inside a (CONTAINING ...) constraint is not a structural part of the parent.
        if (node->type == NodeType::OCTET_STRING && child->type == NodeType::CONSTRAINT && child->name == "Containing") {
            continue;
        }
        // Children of a WITH COMPONENTS constraint are component names, not type references.
        if (node->type == NodeType::CONSTRAINT && node->name == "WithComponents") {
            continue;
        }
        // IMPORTS children are name lists, not type expressions; scope is already built from them.
        if (node->type == NodeType::IMPORTS) {
            continue;
        }
        resolveNodeReferences(child, table, scope, moduleNode, resolution_path);
    }
    for (const auto& param : node->parameters) {
        resolveNodeReferences(param, table, scope, moduleNode, resolution_path);
    }
}

void SymbolTable::resolveReferences(const std::vector<AsnNodePtr>& all_asts) {
    for (auto module_ast : all_asts) {
        if (!module_ast || module_ast->type != NodeType::MODULE) continue;

        std::string currentModuleName = module_ast->name;
        std::unordered_map<std::string, std::string> resolution_scope;

        // 1. Add all symbols defined in this module to the resolution scope.
        auto mod_it = modules.find(currentModuleName);
        if (mod_it != modules.end()) {
            for (const auto& sym_pair : mod_it->second) {
                resolution_scope[sym_pair.first] = currentModuleName + "." + sym_pair.first;
            }
        }

        // 2. Add imported symbols to the resolution scope.
        for (size_t i = 0; i < module_ast->getChildCount(); ++i) {
            auto child = module_ast->getChild(i);
            if (child && child->type == NodeType::IMPORTS) {
                for (size_t j = 0; j < child->getChildCount(); ++j) {
                    auto fromNode = child->getChild(j);
                    const std::string& fromModuleName = fromNode->name;
                    for (size_t k = 0; k < fromNode->getChildCount(); ++k) {
                        auto symbolToImportNode = fromNode->getChild(k);
                        const std::string& symbolToImport = symbolToImportNode->name;

                        if (!isSymbolDefined(fromModuleName, symbolToImport)) {
                            throw std::runtime_error("Symbol '" + symbolToImport + "' not found in imported module '" + fromModuleName + "'. Imported at " + symbolToImportNode->location.toString());
                        }
                        if (resolution_scope.count(symbolToImport)) {
                            throw std::runtime_error("Import collision: symbol '" + symbolToImport + "' is already defined in module '" + currentModuleName + "'. Imported at " + symbolToImportNode->location.toString());
                        }
                        resolution_scope[symbolToImport] = fromModuleName + "." + symbolToImport;
                    }
                }
            }
        }

        // 3. Traverse the module's definitions and resolve references.
        std::unordered_set<std::string> resolution_path;
        resolveNodeReferences(module_ast, *this, resolution_scope, module_ast, resolution_path);

        // 4. Apply AUTOMATIC TAGS: assign sequential implicit tags to untagged members.
        if (module_ast->tagging_environment == AsnNode::TaggingMode::AUTOMATIC) {
            for (size_t i = 0; i < module_ast->getChildCount(); ++i) {
                auto assignment = module_ast->getChild(i);
                if (!assignment || assignment->getChildCount() == 0) continue;
                auto typeNode = assignment->getChild(0);
                if (!typeNode) continue;
                auto effectiveType = typeNode->resolvedTypeNode ? typeNode->resolvedTypeNode : typeNode;
                if (effectiveType->type != NodeType::SEQUENCE && effectiveType->type != NodeType::SET) continue;

                int tagIndex = 0;
                for (size_t j = 0; j < effectiveType->getChildCount(); ++j) {
                    auto member = effectiveType->getChild(j);
                    if (!member || member->type == NodeType::EXTENSION_MARKER) {
                        ++tagIndex;
                        continue;
                    }
                    if (member->getChildCount() == 0) { ++tagIndex; continue; }
                    auto memberType = member->getChild(0);
                    if (!memberType) { ++tagIndex; continue; }
                    if (!memberType->tag.has_value()) {
                        AsnNode::AsnTag autoTag;
                        autoTag.tag_class = AsnNode::TagClass::CONTEXT_SPECIFIC;
                        autoTag.tag_number = tagIndex;
                        autoTag.mode = AsnNode::TaggingMode::IMPLICIT;
                        memberType->tag = autoTag;
                    }
                    ++tagIndex;
                }
            }
        }
    }
}

bool SymbolTable::isSymbolDefined(const std::string& moduleName, const std::string& symbolName) const {
    return modules.count(moduleName) && modules.at(moduleName).count(symbolName);
}

void SymbolTable::printSymbols() const {
    for (const auto& mod_pair : modules) {
        std::cout << "Module: " << mod_pair.first << "\n";
        for (const auto& sym_pair : mod_pair.second) {
            std::cout << "  Symbol: " << sym_pair.first << "\n";
        }
    }
}

} // namespace asn1::frontend
