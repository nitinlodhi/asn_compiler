#include "tests.h"
#include "frontend/AsnLexer.h"
#include <cassert>

using namespace asn1;

bool testLexer() {
    frontend::AsnLexer lexer("SEQUENCE { value INTEGER }", "test.asn1");
    auto tokens = lexer.tokenize();

    assert(tokens.size() >= 5); // SEQUENCE, {, value, INTEGER, }
    assert(tokens[0].type == frontend::TokenType::SEQUENCE);
    assert(tokens[0].lexeme == "SEQUENCE");
    assert(tokens[1].type == frontend::TokenType::LBRACE);
    assert(tokens[2].type == frontend::TokenType::IDENTIFIER);
    assert(tokens[2].lexeme == "value");
    assert(tokens[3].type == frontend::TokenType::INTEGER);
    assert(tokens[4].type == frontend::TokenType::RBRACE);
    return true;
}

void register_lexer_tests(utils::TestFramework& runner) {
    runner.addTest("Lexer: Simple SEQUENCE", testLexer);
}