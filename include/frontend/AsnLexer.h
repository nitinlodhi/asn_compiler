#ifndef ASN1_FRONTEND_ASN_LEXER_H
#define ASN1_FRONTEND_ASN_LEXER_H

#include <string>
#include <vector>
#include <memory>
#include "frontend/Token.h"

namespace asn1::frontend {

class AsnLexer {
private:
    std::string source;
    std::string filename;
    size_t current;
    size_t line;
    size_t column;

    char peek() const;
    char peekNext() const;
    char advance();
    void skipWhitespace();
    void skipComment();
    
    Token scanString();
    Token scanNumber();
    Token scanIdentifier();
    Token scanOperator();
    
    TokenType checkKeyword(const std::string& text) const;

public:
    AsnLexer(const std::string& source, const std::string& filename = "<input>");
    ~AsnLexer() = default;

    Token getNextToken();
    Token peekToken();
    std::vector<Token> tokenize();
};

} // namespace asn1::frontend

#endif // ASN1_FRONTEND_ASN_LEXER_H
