#include "frontend/AsnLexer.h"
#include <cctype>

namespace asn1::frontend {

AsnLexer::AsnLexer(const std::string& source, const std::string& filename)
    : source(source), filename(filename), current(0), line(1), column(1) {}

char AsnLexer::peek() const {
    if (current >= source.length()) return '\0';
    return source[current];
}

char AsnLexer::peekNext() const {
    if (current + 1 >= source.length()) return '\0';
    return source[current + 1];
}

char AsnLexer::advance() {
    char c = peek();
    current++;
    if (c == '\n') {
        line++;
        column = 1;
    } else {
        column++;
    }
    return c;
}

void AsnLexer::skipWhitespace() {
    while (std::isspace(peek())) {
        advance();
    }
}

void AsnLexer::skipComment() {
    if (peek() == '-' && peekNext() == '-') {
        while (peek() != '\n' && peek() != '\0') {
            advance();
        }
    }
}

Token AsnLexer::getNextToken() {
    skipWhitespace();
    
    if (peek() == '-' && peekNext() == '-') {
        skipComment();
        return getNextToken();
    }

    SourceLocation loc(filename, line, column);

    if (peek() == '\0') {
        return Token(TokenType::END_OF_FILE, "EOF", loc);
    }

    if (std::isalpha(peek()) || peek() == '_') {
        return scanIdentifier();
    }

    if (std::isdigit(peek())) {
        return scanNumber();
    }

    if (peek() == '"' || peek() == '\'') {
        return scanString();
    }

    return scanOperator();
}

Token AsnLexer::peekToken() {
    size_t savedCurrent = current;
    unsigned int savedLine = line;
    unsigned int savedColumn = column;
    
    Token token = getNextToken();
    
    current = savedCurrent;
    line = savedLine;
    column = savedColumn;
    
    return token;
}

Token AsnLexer::scanIdentifier() {
    SourceLocation loc(filename, line, column);
    std::string text;

    while (std::isalnum(peek()) || peek() == '_' || peek() == '-') {
        text += advance();
    }

    TokenType type = checkKeyword(text);
    return Token(type, text, loc);
}

Token AsnLexer::scanNumber() {
    SourceLocation loc(filename, line, column);
    std::string text;

    while (std::isdigit(peek())) {
        text += advance();
    }

    return Token(TokenType::NUMBER, text, loc);
}

Token AsnLexer::scanString() {
    SourceLocation loc(filename, line, column);
    char quote = advance();
    std::string text;

    while (peek() != quote && peek() != '\0') {
        text += advance();
    }

    if (peek() == quote) {
        advance();
    }

    return Token(TokenType::STRING, text, loc);
}

Token AsnLexer::scanOperator() {
    SourceLocation loc(filename, line, column);
    char c = advance();

    switch (c) {
        case '(': return Token(TokenType::LPAREN, "(", loc);
        case ')': return Token(TokenType::RPAREN, ")", loc);
        case '{': return Token(TokenType::LBRACE, "{", loc);
        case '}': return Token(TokenType::RBRACE, "}", loc);
        case '[':
            if (peek() == '[') {
                advance();
                return Token(TokenType::LDBRACKET, "[[", loc);
            }
            return Token(TokenType::LBRACKET, "[", loc);
        case ']':
            if (peek() == ']') {
                advance();
                return Token(TokenType::RDBRACKET, "]]", loc);
            }
            return Token(TokenType::RBRACKET, "]", loc);
        case ',': return Token(TokenType::COMMA, ",", loc);
        case '.':
            if (peek() == '.') {
                advance();
                if (peek() == '.') {
                    advance();
                    return Token(TokenType::ELLIPSIS, "...", loc);
                }
                return Token(TokenType::DOTDOT, "..", loc);
            }
            return Token(TokenType::DOT, ".", loc);
        case '|': return Token(TokenType::PIPE, "|", loc);
        case '-': return Token(TokenType::MINUS, "-", loc);
        case '&': return Token(TokenType::AMPERSAND, "&", loc);
        case '@': return Token(TokenType::AT_SIGN, "@", loc);
        case ';': return Token(TokenType::SEMICOLON, ";", loc);
        case '+': return Token(TokenType::PLUS, "+", loc);
        case ':':
            if (peek() == ':') {
                advance(); // consume second ':'
                if (peek() == '=') {
                    advance(); // consume '='
                    return Token(TokenType::ASSIGNMENT, "::=", loc);
                }
                // Put back the second ':' conceptually - but we can't, so return '::' as UNKNOWN for now
                // This is a limitation, but in practice ASN.1 should have '::=' together
                return Token(TokenType::UNKNOWN, "::", loc);
            }
            break;
        default: break;
    }

    return Token(TokenType::UNKNOWN, std::string(1, c), loc);
}

TokenType AsnLexer::checkKeyword(const std::string& text) const {
    if (text == "SEQUENCE") return TokenType::SEQUENCE;
    if (text == "SET") return TokenType::SET;
    if (text == "CHOICE") return TokenType::CHOICE;
    if (text == "OPTIONAL") return TokenType::OPTIONAL;
    if (text == "DEFAULT") return TokenType::DEFAULT;
    if (text == "INTEGER") return TokenType::INTEGER;
    if (text == "BOOLEAN") return TokenType::BOOLEAN;
    if (text == "ENUMERATED") return TokenType::ENUMERATED;
    if (text == "OCTET") return TokenType::OCTET_STRING;
    if (text == "BIT") return TokenType::BIT_STRING;
    if (text == "SIZE") return TokenType::SIZE;
    if (text == "OF") return TokenType::OF;
    if (text == "OBJECT") return TokenType::OBJECT_IDENTIFIER;
    if (text == "DEFINITIONS") return TokenType::DEFINITIONS;
    if (text == "BEGIN") return TokenType::BEGIN;
    if (text == "END") return TokenType::END;
    if (text == "IMPORTS") return TokenType::IMPORTS;
    if (text == "FROM") return TokenType::FROM;
    if (text == "ANY") return TokenType::ANY;
    if (text == "DEFINED") return TokenType::DEFINED;
    if (text == "BY") return TokenType::BY;
    if (text == "CLASS") return TokenType::CLASS;
    if (text == "CONTAINING") return TokenType::CONTAINING;
    if (text == "WITH") return TokenType::WITH;
    if (text == "COMPONENTS") return TokenType::COMPONENTS;
    if (text == "IMPLICIT") return TokenType::IMPLICIT;
    if (text == "EXPLICIT") return TokenType::EXPLICIT;
    if (text == "APPLICATION") return TokenType::APPLICATION;
    if (text == "UNIVERSAL") return TokenType::UNIVERSAL;
    if (text == "PRIVATE") return TokenType::PRIVATE;
    if (text == "SYNTAX") return TokenType::SYNTAX;
    if (text == "NULL") return TokenType::NULL_KEYWORD;
    if (text == "REAL") return TokenType::REAL;
    if (text == "UTF8String") return TokenType::UTF8String;
    if (text == "PrintableString") return TokenType::PrintableString;
    if (text == "VisibleString") return TokenType::VisibleString;
    if (text == "IA5String") return TokenType::IA5String;
    if (text == "NumericString") return TokenType::NumericString;
    if (text == "AUTOMATIC") return TokenType::AUTOMATIC;
    if (text == "TAGS") return TokenType::TAGS;
    if (text == "EXTENSIBILITY") return TokenType::EXTENSIBILITY;
    if (text == "IMPLIED") return TokenType::IMPLIED;
    return TokenType::IDENTIFIER;
}

std::vector<Token> AsnLexer::tokenize() {
    std::vector<Token> tokens;
    while (true) {
        Token token = getNextToken();
        if (token.type != TokenType::WHITESPACE) {
            tokens.push_back(token);
        }
        if (token.type == TokenType::END_OF_FILE) {
            break;
        }
    }
    return tokens;
}

} // namespace asn1::frontend
