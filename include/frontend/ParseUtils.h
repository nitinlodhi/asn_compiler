#ifndef ASN1_FRONTEND_PARSE_UTILS_H
#define ASN1_FRONTEND_PARSE_UTILS_H

#include <string>
#include <optional>
#include <vector>
#include "frontend/Token.h"

namespace asn1::frontend {

class ParseUtils {
public:
    // Utility functions for parsing
    static bool isKeywordToken(const Token& token, const std::string& keyword);
    static bool isOperatorToken(const Token& token, TokenType opType);
    static std::optional<int> parseInteger(const Token& token);
    static std::optional<std::string> parseIdentifier(const Token& token);
    static bool matchSequence(const std::vector<Token>& tokens, 
                              size_t& pos, 
                              const std::vector<TokenType>& expected);
};

} // namespace asn1::frontend

#endif // ASN1_FRONTEND_PARSE_UTILS_H
