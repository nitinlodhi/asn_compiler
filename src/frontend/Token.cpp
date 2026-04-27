#include "frontend/Token.h"

namespace asn1::frontend {

Token::Token(TokenType type, const std::string& lexeme, const SourceLocation& loc)
    : type(type), lexeme(lexeme), location(loc) {}

std::string Token::toString() const {
    return "Token(" + lexeme + ", line " + std::to_string(location.line) + 
           ", col " + std::to_string(location.column) + ")";
}

bool Token::isKeyword() const {
    return type >= TokenType::SEQUENCE && type <= TokenType::NumericString;
}

} // namespace asn1::frontend
