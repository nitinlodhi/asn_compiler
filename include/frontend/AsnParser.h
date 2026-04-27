#ifndef ASN1_FRONTEND_ASN_PARSER_H
#define ASN1_FRONTEND_ASN_PARSER_H

#include <vector>
#include <memory>
#include "frontend/Token.h"
#include "frontend/AsnNode.h"

namespace asn1::frontend {

class AsnParser {
private:
    std::vector<Token> tokens;
    size_t current;

    Token peek() const;
    Token peekNext() const;
    Token advance();
    bool match(TokenType type);
    bool check(TokenType type) const;
    Token consume(TokenType type, const std::string& message);

    AsnNodePtr parseModule();
    AsnNodePtr parseModuleDefinition();
    AsnNodePtr parseImports();
    AsnNodePtr parseAssignment();
    AsnNodePtr parseType();
    AsnNodePtr parseSequence();
    AsnNodePtr parseSet();
    AsnNodePtr parseSetOf();
    AsnNodePtr parseSequenceOf();
    AsnNodePtr parseChoice();
    AsnNodePtr parseInteger();
    AsnNodePtr parseObjectSet();
    AsnNodePtr parseObject();
    AsnNodePtr parseClassDefinition();
    AsnNodePtr parseEnumerated();
    AsnNodePtr parseOctetString();
    AsnNodePtr parseBitString();
    AsnNodePtr parseObjectIdentifier();
    AsnNodePtr parseAny();
    std::optional<AsnNode::AsnTag> parseTag();
    AsnNodePtr parseCharacterString(NodeType nodeType, const std::string& name);
    void parseParameters(const AsnNodePtr& ownerNode);
    AsnNodePtr parseConstraintBound();
    AsnNodePtr parseValue(const AsnNodePtr& type);
    AsnNodePtr parseIntegerValue();
    AsnNodePtr parseObjectIdentifierValue();
    AsnNodePtr parseNull();
    AsnNodePtr parseReal();
    AsnNodePtr parseConstraint();
    AsnNodePtr parseMember();

public:
    AsnParser(const std::vector<Token>& tokens);
    ~AsnParser() = default;

    AsnNodePtr parse();
    std::vector<AsnNodePtr> parseAll();
};

} // namespace asn1::frontend

#endif // ASN1_FRONTEND_ASN_PARSER_H
