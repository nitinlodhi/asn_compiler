#include "frontend/AsnParser.h"
#include <cctype>

namespace asn1::frontend {

AsnParser::AsnParser(const std::vector<Token>& tokens)
    : tokens(tokens), current(0) {}

Token AsnParser::peek() const {
    if (current >= tokens.size()) {
        return Token(TokenType::END_OF_FILE, "EOF", SourceLocation());
    }
    return tokens[current];
}

Token AsnParser::peekNext() const {
    if (current + 1 >= tokens.size()) {
        return Token(TokenType::END_OF_FILE, "EOF", SourceLocation());
    }
    return tokens[current + 1];
}

Token AsnParser::advance() {
    Token token = peek();
    current++;
    return token;
}

bool AsnParser::match(TokenType type) {
    if (check(type)) {
        advance();
        return true;
    }
    return false;
}

bool AsnParser::check(TokenType type) const {
    return peek().type == type;
}

Token AsnParser::consume(TokenType type, const std::string& message) {
    if (check(type)) {
        return advance();
    }
    throw std::runtime_error(message + " at " + peek().location.toString());
}

AsnNodePtr AsnParser::parse() {
    return parseModuleDefinition();
}

std::vector<AsnNodePtr> AsnParser::parseAll() {
    std::vector<AsnNodePtr> modules;
    while (!check(TokenType::END_OF_FILE)) {
        modules.push_back(parseModuleDefinition());
    }
    return modules;
}

AsnNodePtr AsnParser::parseModuleDefinition() {
    Token moduleName = consume(TokenType::IDENTIFIER, "Expected module name.");
    auto module = std::make_shared<AsnNode>(NodeType::MODULE, moduleName.lexeme, moduleName.location);

    // Optional: OID, tags, etc. We'll skip them for now.
    if (check(TokenType::LBRACE)) {
        while(!check(TokenType::RBRACE) && !check(TokenType::END_OF_FILE)) {
            advance();
        }
        consume(TokenType::RBRACE, "Expected '}' for module identifier.");
    }

    consume(TokenType::DEFINITIONS, "Expected 'DEFINITIONS'.");

    // Loop to parse optional module header clauses like AUTOMATIC TAGS
    while (true) {
        if (peek().type == TokenType::AUTOMATIC && peekNext().type == TokenType::TAGS) {
            advance(); // AUTOMATIC
            advance(); // TAGS
            module->tagging_environment = AsnNode::TaggingMode::AUTOMATIC;
        } else if (peek().type == TokenType::EXPLICIT && peekNext().type == TokenType::TAGS) {
            advance(); // EXPLICIT
            advance(); // TAGS
            module->tagging_environment = AsnNode::TaggingMode::EXPLICIT;
        } else if (peek().type == TokenType::IMPLICIT && peekNext().type == TokenType::TAGS) {
            advance(); // IMPLICIT
            advance(); // TAGS
            module->tagging_environment = AsnNode::TaggingMode::IMPLICIT;
        } else if (peek().type == TokenType::EXTENSIBILITY && peekNext().type == TokenType::IMPLIED) {
            advance(); // EXTENSIBILITY
            advance(); // IMPLIED
            module->extensibility_implied = true;
        } else {
            break; // No more optional clauses found
        }
    }
    consume(TokenType::ASSIGNMENT, "Expected '::=' after DEFINITIONS.");
    consume(TokenType::BEGIN, "Expected 'BEGIN'.");

    if (check(TokenType::IMPORTS)) {
        auto importsNode = parseImports();
        if (importsNode) {
            module->addChild(importsNode);
        }
    }

    while (!check(TokenType::END) && !check(TokenType::END_OF_FILE)) {
        auto assignment = parseAssignment();
        if (assignment) {
            module->addChild(assignment);
        }
    }

    consume(TokenType::END, "Expected 'END'.");
    return module;
}

AsnNodePtr AsnParser::parseImports() {
    consume(TokenType::IMPORTS, "Expected 'IMPORTS'.");
    auto importsNode = std::make_shared<AsnNode>(NodeType::IMPORTS, "IMPORTS", peek().location);

    while (check(TokenType::IDENTIFIER)) {
        std::vector<std::string> importedSymbols;
        do {
            Token symbol = consume(TokenType::IDENTIFIER, "Expected imported symbol name.");
            importedSymbols.push_back(symbol.lexeme);
        } while (match(TokenType::COMMA));

        consume(TokenType::FROM, "Expected 'FROM'.");
        Token moduleName = consume(TokenType::IDENTIFIER, "Expected module name to import from.");

        auto fromNode = std::make_shared<AsnNode>(NodeType::ASSIGNMENT, moduleName.lexeme, moduleName.location);
        for (const auto& sym : importedSymbols) {
            fromNode->addChild(std::make_shared<AsnNode>(NodeType::IDENTIFIER, sym, moduleName.location));
        }
        importsNode->addChild(fromNode);
    }
    consume(TokenType::SEMICOLON, "Expected ';' to terminate IMPORTS clause.");
    return importsNode;
}

AsnNodePtr AsnParser::parseAssignment() {
    Token nameToken = consume(TokenType::IDENTIFIER, "Expected assignment name");

    // Handle parameterized type definitions, e.g., "MyType { Param } ::= ..."
    if (check(TokenType::LBRACE)) {
        auto assignment = std::make_shared<AsnNode>(NodeType::ASSIGNMENT, nameToken.lexeme, nameToken.location);
        assignment->isParameterized = true;

        consume(TokenType::LBRACE, "Expected '{' for parameters.");
        while(!check(TokenType::RBRACE)) {
            Token paramName = consume(TokenType::IDENTIFIER, "Expected parameter name.");
            auto paramNode = std::make_shared<AsnNode>(NodeType::IDENTIFIER, paramName.lexeme, paramName.location);
            assignment->parameters.push_back(paramNode);
            if (!match(TokenType::COMMA)) {
                if (!check(TokenType::RBRACE)) {
                    throw std::runtime_error("Expected ',' or '}' in parameter definition list at " + peek().location.toString());
                }
            }
        }
        consume(TokenType::RBRACE, "Expected '}' to close parameters.");

        consume(TokenType::ASSIGNMENT, "Expected '::=' for parameterized type assignment '" + nameToken.lexeme + "'");
        auto type = parseType();
        if (!type) {
            throw std::runtime_error("Missing type definition for parameterized type '" + nameToken.lexeme + "' at " + nameToken.location.toString());
        }
        assignment->addChild(type);
        return assignment;
    }

    // Heuristic: if it starts with an uppercase letter, it's a type assignment.
    // This is the standard convention in ASN.1.
    bool isTypeAssignment = std::isupper(nameToken.lexeme[0]);

    if (isTypeAssignment) {
        // This is a Type Assignment: MyType ::= INTEGER
        consume(TokenType::ASSIGNMENT, "Expected '::=' for type assignment '" + nameToken.lexeme + "'");
        if (check(TokenType::CLASS)) {
            auto classDef = parseClassDefinition();
            auto assignment = std::make_shared<AsnNode>(NodeType::ASSIGNMENT, nameToken.lexeme, nameToken.location);
            assignment->addChild(classDef);
            return assignment;
        }
        auto type = parseType();
        if (!type) {
            throw std::runtime_error("Missing type definition for '" + nameToken.lexeme + "' at " + nameToken.location.toString());
        }
        auto assignment = std::make_shared<AsnNode>(NodeType::ASSIGNMENT, nameToken.lexeme, nameToken.location);
        assignment->addChild(type);
        return assignment;
    } else {
        // This is a Value Assignment: myValue INTEGER ::= 5
        auto type = parseType();
        if (!type) {
            throw std::runtime_error("Expected a type for value assignment '" + nameToken.lexeme + "' at " + nameToken.location.toString());
        }

        if (type->type == NodeType::IDENTIFIER &&
            peek().type == TokenType::ASSIGNMENT && peekNext().type == TokenType::LBRACE) {
            // This is an Object Set Assignment: mySet MY-CLASS ::= { ... }
            consume(TokenType::ASSIGNMENT, "Expected '::=' for object set assignment");
            auto objectSet = parseObjectSet();
            auto assignment = std::make_shared<AsnNode>(NodeType::OBJECT_SET_ASSIGNMENT, nameToken.lexeme, nameToken.location);
            assignment->addChild(type); // The class
            assignment->addChild(objectSet);
            return assignment;
        } else {
            consume(TokenType::ASSIGNMENT, "Expected '::=' for value assignment '" + nameToken.lexeme + "'");
            auto valueNode = parseValue(type);
            auto assignment = std::make_shared<AsnNode>(NodeType::VALUE_ASSIGNMENT, nameToken.lexeme, nameToken.location);
            assignment->addChild(type);
            assignment->addChild(valueNode);
            return assignment;
        }
    }
}

AsnNodePtr AsnParser::parseType() {
    // Handle tagged types: [CLASS number] IMPLICIT/EXPLICIT type
    if (check(TokenType::LBRACKET)) {
        advance(); // consume [
        AsnNode::TagClass tagClass = AsnNode::TagClass::CONTEXT_SPECIFIC;
        if (match(TokenType::APPLICATION)) {
            tagClass = AsnNode::TagClass::APPLICATION;
        } else if (match(TokenType::UNIVERSAL)) {
            tagClass = AsnNode::TagClass::UNIVERSAL;
        } else if (match(TokenType::PRIVATE)) {
            tagClass = AsnNode::TagClass::PRIVATE;
        }
        Token numToken = consume(TokenType::NUMBER, "Expected tag number after '['");
        int tagNumber = std::stoi(numToken.lexeme);
        consume(TokenType::RBRACKET, "Expected ']' to close tag");

        AsnNode::TaggingMode mode = AsnNode::TaggingMode::IMPLICIT;
        if (match(TokenType::IMPLICIT)) {
            mode = AsnNode::TaggingMode::IMPLICIT;
        } else if (match(TokenType::EXPLICIT)) {
            mode = AsnNode::TaggingMode::EXPLICIT;
        }

        auto taggedType = parseType();
        if (!taggedType) {
            throw std::runtime_error("Expected type after tag at " + numToken.location.toString());
        }
        AsnNode::AsnTag tag;
        tag.tag_class = tagClass;
        tag.tag_number = tagNumber;
        tag.mode = mode;
        taggedType->tag = tag;
        return taggedType;
    }

    if (check(TokenType::SEQUENCE)) {
        if (peekNext().type == TokenType::LBRACE) {
            return parseSequence();
        }
        // Otherwise, assume it's a SEQUENCE OF
        return parseSequenceOf();
    } else if (check(TokenType::ANY)) {
        return parseAny();
    } else if (check(TokenType::SET)) {
        if (peekNext().type == TokenType::LBRACE) {
            return parseSet();
        }
        // Otherwise, assume it's a SET OF
        return parseSetOf();
    } else if (check(TokenType::CHOICE)) {
        return parseChoice();
    } else if (check(TokenType::INTEGER)) {
        return parseInteger();
    } else if (check(TokenType::BOOLEAN)) {
        return std::make_shared<AsnNode>(NodeType::BOOLEAN, "BOOLEAN", advance().location);
    } else if (check(TokenType::ENUMERATED)) {
        return parseEnumerated();
    } else if (check(TokenType::OCTET_STRING)) {
        return parseOctetString();
    } else if (check(TokenType::BIT_STRING)) {
        return parseBitString();
    } else if (check(TokenType::OBJECT_IDENTIFIER)) {
        return parseObjectIdentifier();
    } else if (check(TokenType::NULL_KEYWORD)) {
        return parseNull();
    } else if (check(TokenType::REAL)) {
        return parseReal();
    } else if (check(TokenType::UTF8String)) {
        return parseCharacterString(NodeType::UTF8_STRING, "UTF8String");
    } else if (check(TokenType::PrintableString)) {
        return parseCharacterString(NodeType::PRINTABLE_STRING, "PrintableString");
    } else if (check(TokenType::VisibleString)) {
        return parseCharacterString(NodeType::VISIBLE_STRING, "VisibleString");
    } else if (check(TokenType::IA5String)) {
        return parseCharacterString(NodeType::IA5_STRING, "IA5String");
    } else if (check(TokenType::NumericString)) {
        return parseCharacterString(NodeType::NUMERIC_STRING, "NumericString");
    } else if (check(TokenType::IDENTIFIER)) {
        Token id1 = advance();
        AsnNodePtr typeNode;
        if (match(TokenType::DOT)) {
            if (match(TokenType::AMPERSAND)) {
                Token id2 = consume(TokenType::IDENTIFIER, "Expected field name after '&'");
                typeNode = std::make_shared<AsnNode>(NodeType::FIELD_REFERENCE, id1.lexeme, id1.location);
                typeNode->value = id2.lexeme; // Store field name in value
            } else {
                throw std::runtime_error("Unsupported qualified reference, expected '&' after '.' at " + id1.location.toString());
            }
        } else {
            // This is a type reference, e.g., 'MyInteger' in 'value MyInteger'.
            typeNode = std::make_shared<AsnNode>(NodeType::IDENTIFIER, id1.lexeme, id1.location);
        }

        if (check(TokenType::LBRACE)) {
            parseParameters(typeNode);
        }
        if (typeNode->type == NodeType::FIELD_REFERENCE && check(TokenType::LPAREN)) {
            // Table constraint: ({mySet}) or ({mySet}{@id}) — parse inner {…} blocks as parameters.
            advance(); // consume (
            while (check(TokenType::LBRACE)) {
                parseParameters(typeNode);
            }
            consume(TokenType::RPAREN, "Expected ')' to close table constraint");
        } else if (check(TokenType::LPAREN)) {
            auto constraint = parseConstraint();
            if (constraint) {
                typeNode->addChild(constraint);
            }
        }
        return typeNode;
    }

    return nullptr;
}

AsnNodePtr AsnParser::parseSequenceOf() {
    Token seqToken = consume(TokenType::SEQUENCE, "Expected 'SEQUENCE'");
    auto seqOfNode = std::make_shared<AsnNode>(NodeType::SEQUENCE_OF, "SEQUENCE OF", seqToken.location);

    // Optional constraint before 'OF'
    if (check(TokenType::LPAREN)) {
        auto constraint = parseConstraint();
        if (constraint) {
            seqOfNode->addChild(constraint);
        }
    }

    consume(TokenType::OF, "Expected 'OF' after 'SEQUENCE' and optional constraint");

    auto elementType = parseType();
    if (!elementType) {
        throw std::runtime_error("Expected a type for SEQUENCE OF at " + seqToken.location.toString());
    }
    // Insert the element type as the first child for a consistent AST structure.
    // Child 0: Element Type
    // Child 1 (optional): Constraint
    seqOfNode->children.insert(seqOfNode->children.begin(), elementType);

    return seqOfNode;
}

AsnNodePtr AsnParser::parseSet() {
    Token token = advance();
    auto setNode = std::make_shared<AsnNode>(NodeType::SET, "SET", token.location);
    
    consume(TokenType::LBRACE, "Expected '{'");
    
    while (!check(TokenType::RBRACE) && !check(TokenType::END_OF_FILE)) {
        // Version brackets [[ ... ]] are treated as transparent for parsing members,
        // but they imply an extension. The members inside will be parsed as normal.
        if (match(TokenType::LDBRACKET) || match(TokenType::RDBRACKET)) {
            if (match(TokenType::COMMA)) {} // Optional comma after brackets
            continue;
        }

        if (match(TokenType::ELLIPSIS)) {
            if (setNode->hasExtension) {
                throw std::runtime_error("Duplicate '...' in SET at " + peek().location.toString());
            }
            setNode->hasExtension = true;
            setNode->addChild(std::make_shared<AsnNode>(NodeType::EXTENSION_MARKER, "...", peek().location));
            if (match(TokenType::COMMA)) {
                // Optional comma after ...
            }
            continue;
        }

        auto member = parseMember();
        if (member) {
            setNode->addChild(member);
        }
        if (match(TokenType::COMMA)) {
            // Continue
        } else if (check(TokenType::RBRACE)) {
            break; // Optional comma at the end
        }
    }
    
    consume(TokenType::RBRACE, "Expected '}'");
    
    return setNode;
}

AsnNodePtr AsnParser::parseSetOf() {
    Token setToken = consume(TokenType::SET, "Expected 'SET'");
    auto setOfNode = std::make_shared<AsnNode>(NodeType::SET_OF, "SET OF", setToken.location);

    // Optional constraint before 'OF'
    if (check(TokenType::LPAREN)) {
        auto constraint = parseConstraint();
        if (constraint) {
            setOfNode->addChild(constraint);
        }
    }

    consume(TokenType::OF, "Expected 'OF' after 'SET' and optional constraint");

    auto elementType = parseType();
    if (!elementType) {
        throw std::runtime_error("Expected a type for SET OF at " + setToken.location.toString());
    }
    // Insert the element type as the first child for a consistent AST structure.
    // Child 0: Element Type
    // Child 1 (optional): Constraint
    setOfNode->children.insert(setOfNode->children.begin(), elementType);

    return setOfNode;
}

AsnNodePtr AsnParser::parseSequence() {
    Token token = advance();
    auto sequence = std::make_shared<AsnNode>(NodeType::SEQUENCE, "SEQUENCE", token.location);
    
    consume(TokenType::LBRACE, "Expected '{'");
    
    while (!check(TokenType::RBRACE) && !check(TokenType::END_OF_FILE)) {
        // Version brackets [[ ... ]] are treated as transparent for parsing members,
        // but they imply an extension. The members inside will be parsed as normal.
        if (match(TokenType::LDBRACKET) || match(TokenType::RDBRACKET)) {
            if (match(TokenType::COMMA)) {} // Optional comma after brackets
            continue;
        }

        if (match(TokenType::ELLIPSIS)) {
            if (sequence->hasExtension) {
                throw std::runtime_error("Duplicate '...' in SEQUENCE at " + peek().location.toString());
            }
            sequence->hasExtension = true;
            sequence->addChild(std::make_shared<AsnNode>(NodeType::EXTENSION_MARKER, "...", peek().location));
            if (match(TokenType::COMMA)) {
                // Optional comma after ...
            }
            continue;
        }

        auto member = parseMember();
        if (member) {
            sequence->addChild(member);
        }
        if (match(TokenType::COMMA)) {
            // Continue
        } else if (check(TokenType::RBRACE)) {
            break; // Optional comma at the end
        }
    }
    
    consume(TokenType::RBRACE, "Expected '}'");
    
    return sequence;
}

AsnNodePtr AsnParser::parseChoice() {
    Token token = advance();
    auto choice = std::make_shared<AsnNode>(NodeType::CHOICE, "CHOICE", token.location);
    
    consume(TokenType::LBRACE, "Expected '{'");
    
    while (!check(TokenType::RBRACE) && !check(TokenType::END_OF_FILE)) {
        // Version brackets [[ ... ]] are treated as transparent for parsing members,
        // but they imply an extension. The members inside will be parsed as normal.
        if (match(TokenType::LDBRACKET) || match(TokenType::RDBRACKET)) {
            if (match(TokenType::COMMA)) {} // Optional comma after brackets
            continue;
        }

        if (match(TokenType::ELLIPSIS)) {
            if (choice->hasExtension) {
                throw std::runtime_error("Duplicate '...' in CHOICE at " + peek().location.toString());
            }
            choice->hasExtension = true;
            choice->addChild(std::make_shared<AsnNode>(NodeType::EXTENSION_MARKER, "...", peek().location));
            if (match(TokenType::COMMA)) {
                // Optional comma after ...
            }
            continue;
        }

        auto option = parseMember();
        if (option) {
            choice->addChild(option);
        }
        if (match(TokenType::COMMA)) {
            // Continue
        } else if (check(TokenType::RBRACE)) {
            break; // Optional comma at the end
        }
    }
    
    consume(TokenType::RBRACE, "Expected '}'");
    
    return choice;
}

AsnNodePtr AsnParser::parseEnumerated() {
    Token enumToken = consume(TokenType::ENUMERATED, "Expected 'ENUMERATED'");
    auto enumNode = std::make_shared<AsnNode>(NodeType::ENUMERATION, "ENUMERATED", enumToken.location);

    consume(TokenType::LBRACE, "Expected '{' for ENUMERATED type");

    while (!check(TokenType::RBRACE) && !check(TokenType::END_OF_FILE)) {
        // Version brackets [[ ... ]] are treated as transparent for parsing members,
        // but they imply an extension. The members inside will be parsed as normal.
        if (match(TokenType::LDBRACKET) || match(TokenType::RDBRACKET)) {
            if (match(TokenType::COMMA)) {} // Optional comma after brackets
            continue;
        }

        if (match(TokenType::ELLIPSIS)) {
            if (enumNode->hasExtension) {
                throw std::runtime_error("Duplicate '...' in ENUMERATED at " + peek().location.toString());
            }
            enumNode->hasExtension = true;
            enumNode->addChild(std::make_shared<AsnNode>(NodeType::EXTENSION_MARKER, "...", peek().location));
            if (match(TokenType::COMMA)) {
                // Optional comma after ...
            }
            continue;
        }

        Token nameToken = consume(TokenType::IDENTIFIER, "Expected enumerator identifier");
        auto enumerator = std::make_shared<AsnNode>(NodeType::ASSIGNMENT, nameToken.lexeme, nameToken.location);

        if (match(TokenType::LPAREN)) {
            Token valueToken = consume(TokenType::NUMBER, "Expected number for enumerator value");
            consume(TokenType::RPAREN, "Expected ')' after enumerator value");
            enumerator->value = valueToken.lexeme;
        }

        enumNode->addChild(enumerator);

        if (match(TokenType::COMMA)) {
            // continue
        } else if (check(TokenType::RBRACE)) {
            break; // Optional comma at the end
        }
    }

    consume(TokenType::RBRACE, "Expected '}' to close ENUMERATED type");
    return enumNode;
}

AsnNodePtr AsnParser::parseObjectSet() {
    consume(TokenType::LBRACE, "Expected '{' for object set");
    auto setNode = std::make_shared<AsnNode>(NodeType::VALUE_NODE, "OBJECT_SET", peek().location);

    while(!check(TokenType::RBRACE)) {
        auto object = parseObject();
        setNode->addChild(object);
        if (!match(TokenType::COMMA)) {
            if (!check(TokenType::RBRACE)) {
                throw std::runtime_error("Expected ',' or '}' in object set at " + peek().location.toString());
            }
        }
    }

    consume(TokenType::RBRACE, "Expected '}' to close object set");
    return setNode;
}

AsnNodePtr AsnParser::parseObject() {
    consume(TokenType::LBRACE, "Expected '{' for object definition");
    auto objectNode = std::make_shared<AsnNode>(NodeType::OBJECT_DEFINITION, "OBJECT", peek().location);

    while(!check(TokenType::RBRACE)) {
        if (match(TokenType::AMPERSAND)) {
            Token fieldName = consume(TokenType::IDENTIFIER, "Expected field name");
            // Field value can be an integer literal or a type/identifier reference.
            AsnNodePtr fieldValue;
            bool is_negative = check(TokenType::MINUS) && peekNext().type == TokenType::NUMBER;
            if (is_negative || check(TokenType::NUMBER)) {
                fieldValue = parseIntegerValue();
            } else {
                // Type reference or keyword type (e.g., &Type INTEGER or &Type MyType)
                fieldValue = parseType();
                if (!fieldValue) {
                    fieldValue = parseValue(nullptr);
                }
            }
            auto fieldAssignment = std::make_shared<AsnNode>(NodeType::ASSIGNMENT, fieldName.lexeme, fieldName.location);
            fieldAssignment->addChild(fieldValue);
            objectNode->addChild(fieldAssignment);
        }
        if (!match(TokenType::COMMA)) {
            if (!check(TokenType::RBRACE)) {
                throw std::runtime_error("Expected ',' or '}' in object definition at " + peek().location.toString());
            }
        }
    }

    consume(TokenType::RBRACE, "Expected '}' to close object definition");
    return objectNode;
}

AsnNodePtr AsnParser::parseClassDefinition() {
    Token classToken = consume(TokenType::CLASS, "Expected 'CLASS'");
    auto classNode = std::make_shared<AsnNode>(NodeType::CLASS_DEFINITION, "CLASS", classToken.location);
    consume(TokenType::LBRACE, "Expected '{'");

    while (!check(TokenType::RBRACE)) {
        if (match(TokenType::AMPERSAND)) {
            Token fieldName = consume(TokenType::IDENTIFIER, "Expected field name");
            auto fieldType = parseType();
            if (!fieldType) {
                throw std::runtime_error("Expected type for class field '&" + fieldName.lexeme + "' at " + fieldName.location.toString());
            }
            // Use an ASSIGNMENT node to represent the field spec: &name TYPE
            auto fieldSpec = std::make_shared<AsnNode>(NodeType::ASSIGNMENT, fieldName.lexeme, fieldName.location);
            if (fieldType->type == NodeType::IDENTIFIER && fieldType->name == "TYPE") {
                fieldSpec->isTypeField = true;
            }
            fieldSpec->addChild(fieldType);
            classNode->addChild(fieldSpec);
        }

        if (!match(TokenType::COMMA)) {
            if (!check(TokenType::RBRACE)) {
                throw std::runtime_error("Expected ',' or '}' in CLASS definition at " + peek().location.toString());
            }
        }
    }
    consume(TokenType::RBRACE, "Expected '}'");

    // Optional WITH SYNTAX clause, parse and discard for now
    if (match(TokenType::WITH)) {
        consume(TokenType::SYNTAX, "Expected 'SYNTAX'");
        consume(TokenType::LBRACE, "Expected '{'");
        int brace_level = 1;
        while(brace_level > 0 && !check(TokenType::END_OF_FILE)) {
            if (check(TokenType::LBRACE)) brace_level++;
            if (check(TokenType::RBRACE)) brace_level--;
            advance();
        }
    }
    return classNode;
}

AsnNodePtr AsnParser::parseObjectIdentifier() {
    Token objectToken = consume(TokenType::OBJECT_IDENTIFIER, "Expected 'OBJECT'");
    Token identifierToken = consume(TokenType::IDENTIFIER, "Expected 'IDENTIFIER' after 'OBJECT'");
    if (identifierToken.lexeme != "IDENTIFIER") { // This was a bug, it was checking for "STRING"
        throw std::runtime_error("Expected 'IDENTIFIER' after 'OBJECT', but got '" + identifierToken.lexeme + "' at " + identifierToken.location.toString());
    }

    auto oidNode = std::make_shared<AsnNode>(NodeType::OBJECT_IDENTIFIER, "OBJECT IDENTIFIER", objectToken.location);

    // OIDs don't have constraints like SIZE or value ranges.
    // They can be assigned values like { 1 3 6 1 }, which we won't parse for now.

    return oidNode;
}

AsnNodePtr AsnParser::parseAny() {
    Token anyToken = consume(TokenType::ANY, "Expected 'ANY'");
    auto anyNode = std::make_shared<AsnNode>(NodeType::ANY_TYPE, "ANY", anyToken.location);

    if (match(TokenType::DEFINED)) {
        consume(TokenType::BY, "Expected 'BY' after 'DEFINED'");
        Token identifier = consume(TokenType::IDENTIFIER, "Expected identifier after 'DEFINED BY'");
        anyNode->value = identifier.lexeme;
    }

    return anyNode;
}

AsnNodePtr AsnParser::parseCharacterString(NodeType nodeType, const std::string& name) {
    Token token = advance();
    auto stringNode = std::make_shared<AsnNode>(nodeType, name, token.location);

    if (check(TokenType::LPAREN)) {
        auto constraint = parseConstraint();
        if (constraint) {
            stringNode->addChild(constraint);
        }
    }
    return stringNode;
}

void AsnParser::parseParameters(const AsnNodePtr& ownerNode) {
    consume(TokenType::LBRACE, "Expected '{' for parameters.");
    while(!check(TokenType::RBRACE)) {
        if (match(TokenType::AT_SIGN)) {
            Token fieldName = consume(TokenType::IDENTIFIER, "Expected field name after '@'");
            auto paramNode = std::make_shared<AsnNode>(NodeType::RELATIVE_REFERENCE, fieldName.lexeme, fieldName.location);
            ownerNode->parameters.push_back(paramNode);
        } else if (check(TokenType::IDENTIFIER)) {
            Token setName = consume(TokenType::IDENTIFIER, "Expected object set name");
            auto paramNode = std::make_shared<AsnNode>(NodeType::IDENTIFIER, setName.lexeme, setName.location);
            ownerNode->parameters.push_back(paramNode);
        } else {
            throw std::runtime_error("Unexpected token '" + peek().lexeme + "' in parameter list at " + peek().location.toString());
        }
    }
    consume(TokenType::RBRACE, "Expected '}' to close parameters.");
}

AsnNodePtr AsnParser::parseValue(const AsnNodePtr& type) {
    if (!type) {
        // For object sets where type isn't known yet
        // Try to parse as integer, the most common case. This is a heuristic.
        return parseIntegerValue();
    }
    
    switch (type->type) {
        case NodeType::INTEGER:
            return parseIntegerValue();
        case NodeType::OBJECT_IDENTIFIER:
            return parseObjectIdentifierValue();
        // ... other cases
        default:
            throw std::runtime_error("Parsing for value of type '" + type->name + "' is not yet implemented at " + type->location.toString());
    }
}

AsnNodePtr AsnParser::parseIntegerValue() {
    bool is_negative = match(TokenType::MINUS);
    Token numberToken = consume(TokenType::NUMBER, "Expected number for INTEGER value.");
    std::string value_str = (is_negative ? "-" : "") + numberToken.lexeme;
    auto valueNode = std::make_shared<AsnNode>(NodeType::VALUE_NODE, "INTEGER_VALUE", numberToken.location);
    valueNode->value = value_str;
    return valueNode;
}

AsnNodePtr AsnParser::parseObjectIdentifierValue() {
    consume(TokenType::LBRACE, "Expected '{' for OBJECT IDENTIFIER value.");
    auto valueNode = std::make_shared<AsnNode>(NodeType::VALUE_NODE, "OID_VALUE", peek().location);
    
    std::string oid_string;
    while(!check(TokenType::RBRACE) && !check(TokenType::END_OF_FILE)) {
        Token component = advance();
        if (component.type != TokenType::NUMBER && component.type != TokenType::IDENTIFIER) {
            throw std::runtime_error("Expected number or identifier for OID component at " + component.location.toString());
        }
        oid_string += component.lexeme + " ";
    }
    if (!oid_string.empty()) {
        oid_string.pop_back();
    }
    valueNode->value = oid_string;

    consume(TokenType::RBRACE, "Expected '}' to close OBJECT IDENTIFIER value.");
    return valueNode;
}

AsnNodePtr AsnParser::parseNull() {
    Token nullToken = consume(TokenType::NULL_KEYWORD, "Expected 'NULL'");
    auto nullNode = std::make_shared<AsnNode>(NodeType::NULL_TYPE, "NULL", nullToken.location);
    // NULL type has no constraints or values.
    return nullNode;
}

AsnNodePtr AsnParser::parseReal() {
    Token realToken = consume(TokenType::REAL, "Expected 'REAL'");
    auto realNode = std::make_shared<AsnNode>(NodeType::REAL, "REAL", realToken.location);

    // REAL can have constraints, but we'll ignore them for now.

    return realNode;
}

AsnNodePtr AsnParser::parseOctetString() {
    Token octetToken = consume(TokenType::OCTET_STRING, "Expected 'OCTET'");
    Token stringToken = consume(TokenType::IDENTIFIER, "Expected 'STRING' after 'OCTET'");
    if (stringToken.lexeme != "STRING") {
        throw std::runtime_error("Expected 'STRING' after 'OCTET', but got '" + stringToken.lexeme + "' at " + stringToken.location.toString());
    }

    auto octetStringNode = std::make_shared<AsnNode>(NodeType::OCTET_STRING, "OCTET STRING", octetToken.location);

    if (check(TokenType::LPAREN)) {
        auto constraint = parseConstraint();
        if (constraint) {
            octetStringNode->addChild(constraint);
        }
    }

    return octetStringNode;
}

AsnNodePtr AsnParser::parseBitString() {
    Token bitToken = consume(TokenType::BIT_STRING, "Expected 'BIT'");
    Token stringToken = consume(TokenType::IDENTIFIER, "Expected 'STRING' after 'BIT'");
    if (stringToken.lexeme != "STRING") {
        throw std::runtime_error("Expected 'STRING' after 'BIT', but got '" + stringToken.lexeme + "' at " + stringToken.location.toString());
    }

    auto bitStringNode = std::make_shared<AsnNode>(NodeType::BIT_STRING, "BIT STRING", bitToken.location);

    if (check(TokenType::LPAREN)) {
        auto constraint = parseConstraint();
        if (constraint) {
            bitStringNode->addChild(constraint);
        }
    }

    return bitStringNode;
}

AsnNodePtr AsnParser::parseInteger() {
    Token token = advance();
    auto integer = std::make_shared<AsnNode>(NodeType::INTEGER, "INTEGER", token.location);
    
    if (check(TokenType::LPAREN)) {
        auto constraint = parseConstraint();
        if (constraint) {
            integer->addChild(constraint);
        }
    }

    return integer;
}

AsnNodePtr AsnParser::parseConstraintBound() {
    bool is_negative = match(TokenType::MINUS);
    
    if (check(TokenType::NUMBER)) {
        Token numberToken = advance();
        auto valueNode = std::make_shared<AsnNode>(NodeType::VALUE_NODE, "NUMBER_LITERAL", numberToken.location);
        valueNode->value = (is_negative ? "-" : "") + numberToken.lexeme;
        return valueNode;
    } else if (check(TokenType::IDENTIFIER)) {
        if (is_negative) {
            // This is likely invalid ASN.1, e.g., (0..-myValue)
            throw std::runtime_error("Negative value reference is not supported in constraints at " + peek().location.toString());
        }
        Token id1 = advance();
        if (match(TokenType::DOT)) {
            if (match(TokenType::AMPERSAND)) {
                Token id2 = consume(TokenType::IDENTIFIER, "Expected field name after '&'");
                auto fieldRefNode = std::make_shared<AsnNode>(NodeType::FIELD_REFERENCE, id1.lexeme, id1.location);
                fieldRefNode->value = id2.lexeme; // Store field name in value
                return fieldRefNode;
            } else {
                throw std::runtime_error("Unsupported qualified reference, expected '&' after '.' at " + id1.location.toString());
            }
        }

        // This is a value reference. The resolver will check if it's a valid value identifier.
        return std::make_shared<AsnNode>(NodeType::IDENTIFIER, id1.lexeme, id1.location);
    } else {
        throw std::runtime_error("Expected number or identifier for constraint bound at " + peek().location.toString());
    }
}

AsnNodePtr AsnParser::parseConstraint() {
    // A constraint is `( <spec> )`
    Token lparen = consume(TokenType::LPAREN, "Expected '(' for constraint.");
    AsnNodePtr constraintNode = nullptr;

    if (match(TokenType::SIZE)) {
        // This is a SIZE constraint: (SIZE(min..max)) or (SIZE(val))
        consume(TokenType::LPAREN, "Expected '(' after 'SIZE'.");
        constraintNode = std::make_shared<AsnNode>(NodeType::CONSTRAINT, "SizeRange", lparen.location);

        constraintNode->addChild(parseConstraintBound());
        if (match(TokenType::DOTDOT)) {
            constraintNode->addChild(parseConstraintBound());
        }
        consume(TokenType::RPAREN, "Expected ')' to close size constraint range.");
    } else if (match(TokenType::WITH)) {
        // This is a WITH COMPONENTS constraint: (WITH COMPONENTS { a, b, ... })
        consume(TokenType::COMPONENTS, "Expected 'COMPONENTS' after 'WITH'");
        consume(TokenType::LBRACE, "Expected '{' after 'WITH COMPONENTS'");
        constraintNode = std::make_shared<AsnNode>(NodeType::CONSTRAINT, "WithComponents", lparen.location);
        while (!check(TokenType::RBRACE) && !check(TokenType::END_OF_FILE)) {
            if (match(TokenType::ELLIPSIS)) {
                constraintNode->hasExtension = true;
            } else {
                Token compName = consume(TokenType::IDENTIFIER, "Expected component name in WITH COMPONENTS");
                constraintNode->addChild(std::make_shared<AsnNode>(NodeType::IDENTIFIER, compName.lexeme, compName.location));
            }
            if (!match(TokenType::COMMA)) break;
        }
        consume(TokenType::RBRACE, "Expected '}' to close WITH COMPONENTS");
    } else if (match(TokenType::CONTAINING)) {
        // This is a CONTAINING constraint: (CONTAINING Type)
        constraintNode = std::make_shared<AsnNode>(NodeType::CONSTRAINT, "Containing", lparen.location);
        auto containedType = parseType();
        if (!containedType) {
            throw std::runtime_error("Expected a type after 'CONTAINING' at " + peek().location.toString());
        }
        constraintNode->addChild(containedType);
    } else {
        // It's a value constraint. It could be a range or a table constraint.
        // Look ahead to see if it's a field reference.
        size_t saved_pos = current;
        auto bound1 = parseConstraintBound();
        bool isTableConstraint = (bound1->type == NodeType::FIELD_REFERENCE && !check(TokenType::DOTDOT));
        current = saved_pos;

        if (isTableConstraint) {
            constraintNode = std::make_shared<AsnNode>(NodeType::CONSTRAINT, "TableConstraint", lparen.location);
            constraintNode->addChild(parseConstraintBound()); // Re-parse it
        } else {
            // Assume it's a value range constraint: (min..max) or (val)
            constraintNode = std::make_shared<AsnNode>(NodeType::CONSTRAINT, "ValueRange", lparen.location);
            constraintNode->addChild(parseConstraintBound());
            if (match(TokenType::DOTDOT)) {
                constraintNode->addChild(parseConstraintBound());
            }
        }
    }
    
    consume(TokenType::RPAREN, "Expected ')' to close constraint.");
    return constraintNode;
}

AsnNodePtr AsnParser::parseMember() {
    // A member is 'identifier Type', e.g. 'rrc-TransactionIdentifier INTEGER'
    Token nameToken = consume(TokenType::IDENTIFIER, "Expected member identifier");

    auto type = parseType();
    if (!type) {
        throw std::runtime_error("Expected a type for member '" + nameToken.lexeme + "' at " + nameToken.location.toString());
    }

    // We use NodeType::ASSIGNMENT to represent a member, where the node's name is the member's name
    // and its child is the member's type.
    auto member = std::make_shared<AsnNode>(NodeType::ASSIGNMENT, nameToken.lexeme, nameToken.location);
    member->addChild(type);

    if (match(TokenType::OPTIONAL)) {
        member->isOptional = true;
    } else if (match(TokenType::DEFAULT)) {
        member->hasDefault = true;
        std::string default_value_str;
        bool is_negative = match(TokenType::MINUS);
        if (is_negative) {
            default_value_str += "-";
        }

        Token defaultValueToken = peek();
        if (defaultValueToken.type == TokenType::NUMBER) {
            default_value_str += defaultValueToken.lexeme;
            member->value = default_value_str;
            advance();
        } else if (defaultValueToken.type == TokenType::IDENTIFIER) {
            if (is_negative) {
                throw std::runtime_error("Cannot have a negative default value for an identifier at " + defaultValueToken.location.toString());
            }
            member->value = defaultValueToken.lexeme;
            member->defaultValueIsIdentifier = true;
            advance();
        } else if (defaultValueToken.type == TokenType::STRING) {
            if (is_negative) {
                throw std::runtime_error("Cannot have a negative default value for a string at " + defaultValueToken.location.toString());
            }
            // Wrap in quotes for C++ literal generation
            member->value = "\"" + defaultValueToken.lexeme + "\"";
            advance();
        } else {
            throw std::runtime_error("Expected a default value (number, identifier, or string) after DEFAULT keyword at " + defaultValueToken.location.toString());
        }
    }

    return member;
}

} // namespace asn1::frontend
