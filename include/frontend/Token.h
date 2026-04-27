#ifndef ASN1_FRONTEND_TOKEN_H
#define ASN1_FRONTEND_TOKEN_H

#include <string>
#include <optional>
#include "frontend/SourceLocation.h"

namespace asn1::frontend {

enum class TokenType {
    // Keywords
    SEQUENCE,
    SET,
    CHOICE,
    OPTIONAL,
    DEFAULT,
    INTEGER,
    BOOLEAN,
    ENUMERATED,
    OCTET_STRING,
    BIT_STRING,
    SIZE,
    OF,
    OBJECT_IDENTIFIER,
    DEFINITIONS,
    BEGIN,
    END,
    IMPORTS,
    FROM,
    ANY,
    DEFINED,
    BY,
    CLASS,
    CONTAINING,
    WITH,
    COMPONENTS,
    IMPLICIT,
    EXPLICIT,
    APPLICATION,
    UNIVERSAL,
    PRIVATE,
    SYNTAX,
    NULL_KEYWORD,
    REAL,
    UTF8String,
    PrintableString,
    VisibleString,
    IA5String,
    NumericString,
    
    AUTOMATIC,
    TAGS,
    EXTENSIBILITY,
    IMPLIED,
    
    // Operators and punctuation
    LPAREN,
    RPAREN,
    LBRACE,
    RBRACE,
    LBRACKET,
    RBRACKET,
    LDBRACKET,         // [[
    RDBRACKET,         // ]]
    COMMA,
    DOT,
    DOTDOT,
    PIPE,
    ASSIGNMENT,
    MINUS,
    AT_SIGN,
    AMPERSAND,
    PLUS,
    SEMICOLON,
    
    // Special
    ELLIPSIS,          // ...
    IDENTIFIER,
    NUMBER,
    STRING,
    COMMENT,
    WHITESPACE,
    END_OF_FILE,
    UNKNOWN,
};

class Token {
public:
    TokenType type;
    std::string lexeme;
    std::optional<std::string> value;
    SourceLocation location;

    Token(TokenType type, const std::string& lexeme, const SourceLocation& loc);
    ~Token() = default;

    std::string toString() const;
    bool isKeyword() const;
};

} // namespace asn1::frontend

#endif // ASN1_FRONTEND_TOKEN_H
