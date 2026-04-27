# Frontend: ASN.1 Schema Parsing (lexer + parser + resolver in one library)

add_library(frontend STATIC
    AsnLexer.cpp
    SourceLocation.cpp
    Token.cpp
    AsnNode.cpp
    AsnParser.cpp
    ParseUtils.cpp
    ConstraintResolver.cpp
    SymbolTable.cpp
)
target_include_directories(frontend PUBLIC ${CMAKE_SOURCE_DIR}/include)
