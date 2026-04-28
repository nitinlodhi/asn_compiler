#include "frontend/ConstraintResolver.h"
#include "frontend/AsnTypeInfo.h"
#include "runtime/uper/RangeUtils.h"
#include "frontend/SymbolTable.h"
#include <cmath>
#include <stdexcept>

namespace asn1::frontend {

AsnTypeInfo::AsnTypeInfo(const std::string& typeName)
    : typeName(typeName), isFixedSize(false), hasExtension(false) {}

void AsnTypeInfo::setFixedSize(bool fixed, int bits) {
    isFixedSize = fixed;
    minBits = bits;
    maxBits = bits;
}

void AsnTypeInfo::setRangeConstraint(long long minVal, long long maxVal) {
    minValue = minVal;
    maxValue = maxVal;
}

int AsnTypeInfo::calculateBitWidth() const {
    if (minBits && maxBits && *minBits == *maxBits) {
        return *minBits;
    }
    
    if (minValue && maxValue) {
        long long range = *maxValue - *minValue + 1;
        if (range <= 0) return 0;
        return static_cast<int>(std::ceil(std::log2(range)));
    }
    
    return -1; // Unknown
}

std::string AsnTypeInfo::toString() const {
    return "AsnTypeInfo(" + typeName + ")";
}

std::optional<long long> ConstraintResolver::resolveBound(const AsnNodePtr& boundNode, const SymbolTable& table, const std::string& moduleName) {
    if (boundNode->type == NodeType::VALUE_NODE) {
        return std::stoll(boundNode->value.value());
    }
    if (boundNode->type == NodeType::IDENTIFIER) {
        auto valueAssignmentNode = table.lookupSymbol(moduleName, boundNode->name);
        // If not in the current module, try the module it was imported from.
        if ((!valueAssignmentNode || valueAssignmentNode->type != NodeType::VALUE_ASSIGNMENT)
            && boundNode->resolvedName.has_value()) {
            const std::string& qual = boundNode->resolvedName.value();
            size_t dp = qual.find('.');
            if (dp != std::string::npos)
                valueAssignmentNode = table.lookupSymbol(qual.substr(0, dp), qual.substr(dp + 1));
        }
        // Last resort: search all modules (handles unresolved cross-module refs in parameterized type bodies)
        if (!valueAssignmentNode || valueAssignmentNode->type != NodeType::VALUE_ASSIGNMENT) {
            auto globalResult = table.findSymbolInAnyModule(boundNode->name);
            if (globalResult.has_value()) valueAssignmentNode = globalResult->second;
        }
        // Unresolvable: treat as a parameter placeholder — signal caller to skip this constraint
        if (!valueAssignmentNode || valueAssignmentNode->type != NodeType::VALUE_ASSIGNMENT) {
            return std::nullopt;
        }
        // The value assignment has two children: type and value. We need the value.
        auto valueNode = valueAssignmentNode->getChild(1);
        if (!valueNode || valueNode->type != NodeType::VALUE_NODE) {
            return std::nullopt;
        }
        return std::stoll(valueNode->value.value());
    }
    return std::nullopt;
}

AsnTypeInfoPtr ConstraintResolver::resolveConstraints(const AsnNodePtr& typeNode, const SymbolTable& table, const std::string& moduleName) {
    if (!typeNode) {
        return nullptr;
    }

    auto typeInfo = std::make_shared<AsnTypeInfo>(typeNode->name);

    // Find the constraint node. It can be a child of INTEGER or SEQUENCE OF.
    AsnNodePtr constraintNode = nullptr;
    for (size_t i = 0; i < typeNode->getChildCount(); ++i) {
        if (typeNode->getChild(i)->type == NodeType::CONSTRAINT) {
            constraintNode = typeNode->getChild(i);
            break;
        }
    }

    if (constraintNode) {
        if (constraintNode->name == "ValueRange" || constraintNode->name == "SizeRange") {
            if (constraintNode->getChildCount() > 0) {
                auto minOpt = resolveBound(constraintNode->getChild(0), table, moduleName);
                if (minOpt.has_value()) {
                    long long minVal = *minOpt;
                    long long maxVal = minVal;
                    if (constraintNode->getChildCount() > 1) {
                        auto maxOpt = resolveBound(constraintNode->getChild(1), table, moduleName);
                        if (maxOpt.has_value()) maxVal = *maxOpt;
                        else maxVal = minVal; // unresolvable upper bound → treat as fixed
                    }
                    typeInfo->setRangeConstraint(minVal, maxVal);
                }
                // If lower bound is unresolvable (parameter placeholder), skip constraint entirely
            }
        } else if (constraintNode->name == "TableConstraint") {
            if (constraintNode->getChildCount() == 1 && constraintNode->getChild(0)->type == NodeType::FIELD_REFERENCE) {
                auto fieldRefNode = constraintNode->getChild(0);
                const std::string& objectSetName = fieldRefNode->name;
                const std::string& fieldName = fieldRefNode->value.value();

                auto objectSetAssignment = table.lookupSymbol(moduleName, objectSetName);
                if (!objectSetAssignment || objectSetAssignment->type != NodeType::OBJECT_SET_ASSIGNMENT) {
                    throw std::runtime_error("Undefined object set '" + objectSetName + "' at " + fieldRefNode->location.toString());
                }

                auto objectSetNode = objectSetAssignment->getChild(1);
                
                std::vector<long long> values;
                for (size_t i = 0; i < objectSetNode->getChildCount(); ++i) {
                    auto objectDefNode = objectSetNode->getChild(i);
                    if (objectDefNode->type != NodeType::OBJECT_DEFINITION) continue;

                    for (size_t j = 0; j < objectDefNode->getChildCount(); ++j) {
                        auto fieldAssignmentNode = objectDefNode->getChild(j);
                        if (fieldAssignmentNode->name == fieldName) {
                            auto fieldValueNode = fieldAssignmentNode->getChild(0);
                            if (fieldValueNode && fieldValueNode->type == NodeType::VALUE_NODE && fieldValueNode->value.has_value()) {
                                values.push_back(std::stoll(fieldValueNode->value.value()));
                            }
                            break;
                        }
                    }
                }

                if (values.empty()) {
                    throw std::runtime_error("No values found for field '&" + fieldName + "' in object set '" + objectSetName + "'");
                }

                long long minVal = *std::min_element(values.begin(), values.end());
                long long maxVal = *std::max_element(values.begin(), values.end());
                typeInfo->setRangeConstraint(minVal, maxVal);
            }
        }
    }

    return typeInfo;
}

} // namespace asn1::frontend
