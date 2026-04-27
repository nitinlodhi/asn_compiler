#ifndef ASN1_FRONTEND_ASN_NODE_H
#define ASN1_FRONTEND_ASN_NODE_H

#include <string>
#include <vector>
#include <memory>
#include <map>
#include <variant>
#include <optional>
#include "frontend/SourceLocation.h"

namespace asn1::frontend {

// Forward declarations
class AsnNode;
using AsnNodePtr = std::shared_ptr<AsnNode>;

enum class NodeType {
    MODULE,
    ASSIGNMENT,
    VALUE_ASSIGNMENT,
    OBJECT_SET_ASSIGNMENT,
    OBJECT_DEFINITION,
    CLASS_DEFINITION,
    TYPE_DEFINITION,
    SEQUENCE,
    SET,
    SET_OF,
    SEQUENCE_OF,
    CHOICE,
    INTEGER,
    BOOLEAN,
    ENUMERATION,
    OCTET_STRING,
    BIT_STRING,
    OBJECT_IDENTIFIER,
    NULL_TYPE,
    ANY_TYPE,
    UTF8_STRING,
    PRINTABLE_STRING,
    VISIBLE_STRING,
    IA5_STRING,
    NUMERIC_STRING,
    REAL,
    CONSTRAINT,
    EXTENSION_MARKER,
    FIELD_REFERENCE,
    IMPORTS,
    RELATIVE_REFERENCE,
    IDENTIFIER,
    VALUE_NODE,
};

class AsnNode {
public:
    NodeType type;
    std::string name;
    SourceLocation location;
    std::vector<AsnNodePtr> children;
    std::vector<AsnNodePtr> parameters;
    std::optional<std::string> value;
    std::optional<std::string> resolvedName;
    bool isOptional = false;
    AsnNodePtr resolvedTypeNode;
    std::optional<std::string> definingFieldName;
    bool isTypeField = false;
    std::map<long long, AsnNodePtr> openTypeMap;
    bool isParameterized = false;
    bool defaultValueIsIdentifier = false;
    bool hasDefault = false;

    enum class TagClass {
        UNIVERSAL,
        APPLICATION,
        PRIVATE,
        CONTEXT_SPECIFIC
    };

    enum class TaggingMode {
        IMPLICIT,
        EXPLICIT,
        AUTOMATIC // Default ASN.1 behavior
    };

    TaggingMode tagging_environment = TaggingMode::EXPLICIT;
    bool extensibility_implied = false;

    struct AsnTag {
        TagClass tag_class = TagClass::CONTEXT_SPECIFIC;
        int tag_number = -1;
        TaggingMode mode = TaggingMode::EXPLICIT; // Default for explicit tags
    };
    std::optional<AsnTag> tag;
    std::optional<int> tag_number;
    bool hasExtension = false;

    AsnNode(NodeType type, const std::string& name, const SourceLocation& loc);
    virtual ~AsnNode() = default;

    void addChild(AsnNodePtr child);
    AsnNodePtr getChild(size_t index) const;
    size_t getChildCount() const;
    
    std::string toString() const;
    std::string toDebugString(int indent = 0) const;
    AsnNodePtr deepCopy() const;
};

} // namespace asn1::frontend

#endif // ASN1_FRONTEND_ASN_NODE_H
