#include "frontend/ParseUtils.h"

namespace asn1::frontend {

bool ParseUtils::isKeywordToken(const Token& token, const std::string& keyword) {
    return token.isKeyword() && token.lexeme == keyword;
}

bool ParseUtils::isOperatorToken(const Token& token, TokenType opType) {
    return token.type == opType;
}

std::optional<int> ParseUtils::parseInteger(const Token& token) {
    if (token.type != TokenType::NUMBER) {
        return std::nullopt;
    }
    try {
        return std::stoi(token.lexeme);
    } catch (...) {
        return std::nullopt;
    }
}

std::optional<std::string> ParseUtils::parseIdentifier(const Token& token) {
    if (token.type == TokenType::IDENTIFIER) {
        return token.lexeme;
    }
    return std::nullopt;
}

bool ParseUtils::matchSequence(const std::vector<Token>& tokens, 
                               size_t& pos, 
                               const std::vector<TokenType>& expected) {
    size_t savedPos = pos;
    for (TokenType type : expected) {
        if (pos >= tokens.size() || tokens[pos].type != type) {
            pos = savedPos;
            return false;
        }
        pos++;
    }
    return true;
}

} // namespace asn1::frontend
