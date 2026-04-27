#include "frontend/AsnNode.h"

namespace asn1::frontend {

AsnNode::AsnNode(NodeType type, const std::string& name, const SourceLocation& loc)
    : type(type), name(name), location(loc) {}

void AsnNode::addChild(AsnNodePtr child) {
    if (child) {
        children.push_back(child);
    }
}

AsnNodePtr AsnNode::getChild(size_t index) const {
    if (index < children.size()) {
        return children[index];
    }
    return nullptr;
}

size_t AsnNode::getChildCount() const {
    return children.size();
}

std::string AsnNode::toString() const {
    return "Node(" + name + ")";
}

std::string AsnNode::toDebugString(int indent) const {
    std::string result(indent * 2, ' ');
    result += "AsnNode(type=" + std::to_string(static_cast<int>(type));
    result += ", name=" + name + ")\n";
    
    for (const auto& child : children) {
        result += child->toDebugString(indent + 1);
    }
    
    return result;
}

AsnNodePtr AsnNode::deepCopy() const {
    auto newNode = std::make_shared<AsnNode>(*this); // Shallow copy members
    newNode->children.clear();
    newNode->parameters.clear();
    newNode->resolvedTypeNode = nullptr; // Avoid dangling pointers
    
    for (const auto& child : this->children) {
        newNode->addChild(child->deepCopy());
    }
    for (const auto& param : this->parameters) {
        // Parameters are also nodes, so they need deep copying.
        newNode->parameters.push_back(param->deepCopy());
    }
    return newNode;
}

} // namespace asn1::frontend
