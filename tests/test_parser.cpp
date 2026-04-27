#include "tests.h"
#include "frontend/AsnLexer.h"
#include "frontend/AsnParser.h"
#include "frontend/SymbolTable.h"
#include <cassert>

using namespace asn1;

bool testParser() {
    // This test validates the fix for parsing SEQUENCE members.
    const char* source = "TestModule DEFINITIONS ::= BEGIN MySeq ::= SEQUENCE { value INTEGER } END";
    frontend::AsnLexer lexer(source, "test.asn1");
    auto tokens = lexer.tokenize();
    frontend::AsnParser parser(tokens);
    auto module = parser.parse();
    assert(module->name == "TestModule");
    assert(module != nullptr);
    assert(module->getChildCount() == 1); // MySeq
    auto mySeq = module->getChild(0);
    assert(mySeq->getChild(0)->getChildCount() == 1); // SEQUENCE should have one member
    return true;
}

bool testParserWithConstraint() {
    // This test validates parsing an INTEGER with a value range constraint.
    const char* source = "TestModule DEFINITIONS ::= BEGIN MyInt ::= INTEGER (0..255) END";
    frontend::AsnLexer lexer(source, "test.asn1");
    auto tokens = lexer.tokenize();
    frontend::AsnParser parser(tokens);
    auto module = parser.parse();
    assert(module->name == "TestModule");
    assert(module != nullptr);
    assert(module->getChildCount() == 1); // MyInt assignment

    auto myIntAssignment = module->getChild(0);
    assert(myIntAssignment->name == "MyInt");
    assert(myIntAssignment->type == frontend::NodeType::ASSIGNMENT);
    assert(myIntAssignment->getChildCount() == 1); // INTEGER type

    auto integerNode = myIntAssignment->getChild(0);
    assert(integerNode->name == "INTEGER");
    assert(integerNode->type == frontend::NodeType::INTEGER);
    assert(integerNode->getChildCount() == 1); // CONSTRAINT node

    auto constraintNode = integerNode->getChild(0);
    assert(constraintNode->type == frontend::NodeType::CONSTRAINT);
    assert(constraintNode->name == "ValueRange");
    assert(constraintNode->getChildCount() == 2);
    auto minBound = constraintNode->getChild(0);
    assert(minBound->type == frontend::NodeType::VALUE_NODE);
    assert(minBound->value.value() == "0");
    auto maxBound = constraintNode->getChild(1);
    assert(maxBound->type == frontend::NodeType::VALUE_NODE);
    assert(maxBound->value.value() == "255");

    return true;
}

bool testParserWithAnyDefinedBy() {
    const char* source =
        "TestModule DEFINITIONS ::= BEGIN\n"
        "  MyMessage ::= SEQUENCE {\n"
        "    messageType   INTEGER,\n"
        "    messageBody   ANY DEFINED BY messageType\n"
        "  }\n"
        "END";
    frontend::AsnLexer lexer(source, "test.asn1");
    auto tokens = lexer.tokenize();
    frontend::AsnParser parser(tokens);
    auto module = parser.parse();
    assert(module != nullptr);

    auto seqAssign = module->getChild(0);
    auto seqNode = seqAssign->getChild(0);
    assert(seqNode->getChildCount() == 2);

    auto anyMember = seqNode->getChild(1);
    assert(anyMember->name == "messageBody");
    auto anyTypeNode = anyMember->getChild(0);
    assert(anyTypeNode->type == frontend::NodeType::ANY_TYPE);
    assert(anyTypeNode->value.has_value() && anyTypeNode->value.value() == "messageType");
    return true;
}

bool testParserWithValueReferenceConstraint() {
    const char* source = 
        "TestModule DEFINITIONS ::= BEGIN\n"
        "  maxThings INTEGER ::= 1024\n"
        "  MyType ::= INTEGER (0..maxThings)\n"
        "END";
    frontend::AsnLexer lexer(source, "test.asn1");
    auto tokens = lexer.tokenize();
    frontend::AsnParser parser(tokens);
    auto module = parser.parse();
    assert(module != nullptr);
    assert(module->getChildCount() == 2);

    auto typeAssign = module->getChild(1);
    assert(typeAssign->name == "MyType");
    auto integerNode = typeAssign->getChild(0);
    assert(integerNode->type == frontend::NodeType::INTEGER);
    assert(integerNode->getChildCount() == 1);

    auto constraintNode = integerNode->getChild(0);
    assert(constraintNode->type == frontend::NodeType::CONSTRAINT);
    assert(constraintNode->name == "ValueRange");
    assert(constraintNode->getChildCount() == 2);

    auto minBound = constraintNode->getChild(0);
    assert(minBound->type == frontend::NodeType::VALUE_NODE);
    assert(minBound->value.value() == "0");

    auto maxBound = constraintNode->getChild(1);
    assert(maxBound->type == frontend::NodeType::IDENTIFIER);
    assert(maxBound->name == "maxThings");

    return true;
}

bool testParserWithReference() {
    const char* source =
        "TestModule DEFINITIONS ::= BEGIN\n"
        "  MyInt ::= INTEGER (0..255)\n"
        "  MySeq ::= SEQUENCE { value MyInt }\n"
        "END";
    frontend::AsnLexer lexer(source, "test.asn1");
    auto tokens = lexer.tokenize();
    frontend::AsnParser parser(tokens);
    auto module = parser.parse();
    assert(module->name == "TestModule");
    assert(module != nullptr);
    assert(module->getChildCount() == 2);

    auto mySeqAssignment = module->getChild(1);
    auto seqNode = mySeqAssignment->getChild(0);
    auto memberNode = seqNode->getChild(0);
    auto typeRefNode = memberNode->getChild(0);

    assert(typeRefNode->name == "MyInt");
    assert(typeRefNode->type == frontend::NodeType::IDENTIFIER);

    return true;
}

bool testParserWithOptional() {
    const char* source = "TestModule DEFINITIONS ::= BEGIN MySeq ::= SEQUENCE { mandatory INTEGER, optional BOOLEAN OPTIONAL } END";
    frontend::AsnLexer lexer(source, "test.asn1");
    auto tokens = lexer.tokenize();
    frontend::AsnParser parser(tokens);
    auto module = parser.parse();
    assert(module->name == "TestModule");
    assert(module != nullptr);
    assert(module->getChildCount() == 1);

    auto mySeqAssignment = module->getChild(0);
    auto seqNode = mySeqAssignment->getChild(0);
    assert(seqNode->type == frontend::NodeType::SEQUENCE);
    assert(seqNode->getChildCount() == 2);

    auto mandatoryMember = seqNode->getChild(0);
    assert(mandatoryMember->name == "mandatory");
    assert(mandatoryMember->isOptional == false);

    auto optionalMember = seqNode->getChild(1);
    assert(optionalMember->name == "optional");
    assert(optionalMember->isOptional == true);

    return true;
}

bool testParserWithExtension() {
    const char* source = "TestModule DEFINITIONS ::= BEGIN MySeq ::= SEQUENCE { root INTEGER, ..., ext BOOLEAN } END";
    frontend::AsnLexer lexer(source, "test.asn1");
    auto tokens = lexer.tokenize();
    frontend::AsnParser parser(tokens);
    auto module = parser.parse();
    assert(module != nullptr);

    auto mySeqAssignment = module->getChild(0);
    auto seqNode = mySeqAssignment->getChild(0);
    assert(seqNode->type == frontend::NodeType::SEQUENCE);
    assert(seqNode->hasExtension == true);
    assert(seqNode->getChildCount() == 3); // root, ..., ext

    auto rootMember = seqNode->getChild(0);
    assert(rootMember->name == "root");

    auto extensionMarker = seqNode->getChild(1);
    assert(extensionMarker->type == frontend::NodeType::EXTENSION_MARKER);

    return true;
}

bool testParserWithChoice() {
    const char* source = "TestModule DEFINITIONS ::= BEGIN MyChoice ::= CHOICE { anInt INTEGER, aBool BOOLEAN } END";
    frontend::AsnLexer lexer(source, "test.asn1");
    auto tokens = lexer.tokenize();
    frontend::AsnParser parser(tokens);
    auto module = parser.parse();
    assert(module != nullptr);

    auto myChoiceAssignment = module->getChild(0);
    assert(myChoiceAssignment->name == "MyChoice");
    auto choiceNode = myChoiceAssignment->getChild(0);
    assert(choiceNode->type == frontend::NodeType::CHOICE);
    assert(choiceNode->getChildCount() == 2);

    auto intOption = choiceNode->getChild(0);
    assert(intOption->name == "anInt");
    assert(intOption->getChild(0)->type == frontend::NodeType::INTEGER);

    auto boolOption = choiceNode->getChild(1);
    assert(boolOption->name == "aBool");
    assert(boolOption->getChild(0)->type == frontend::NodeType::BOOLEAN);

    return true;
}

bool testParserWithSequenceOf() {
    const char* source = "TestModule DEFINITIONS ::= BEGIN MyList ::= SEQUENCE OF INTEGER END";
    frontend::AsnLexer lexer(source, "test.asn1");
    auto tokens = lexer.tokenize();
    frontend::AsnParser parser(tokens);
    auto module = parser.parse();
    assert(module != nullptr);

    auto mySeqOfAssignment = module->getChild(0);
    assert(mySeqOfAssignment->name == "MyList");
    auto seqOfNode = mySeqOfAssignment->getChild(0);
    assert(seqOfNode->type == frontend::NodeType::SEQUENCE_OF);
    assert(seqOfNode->getChildCount() == 1);

    auto elementType = seqOfNode->getChild(0);
    assert(elementType->type == frontend::NodeType::INTEGER);

    return true;
}

bool testParserWithSequenceOfSizeConstraint() {
    const char* source = "TestModule DEFINITIONS ::= BEGIN MyList ::= SEQUENCE (SIZE(1..10)) OF INTEGER END";
    frontend::AsnLexer lexer(source, "test.asn1");
    auto tokens = lexer.tokenize();
    frontend::AsnParser parser(tokens);
    auto module = parser.parse();
    assert(module != nullptr);

    auto mySeqOfAssignment = module->getChild(0);
    assert(mySeqOfAssignment->name == "MyList");
    auto seqOfNode = mySeqOfAssignment->getChild(0);
    assert(seqOfNode->type == frontend::NodeType::SEQUENCE_OF);
    assert(seqOfNode->getChildCount() == 2); // Element type and constraint

    auto elementType = seqOfNode->getChild(0);
    assert(elementType->type == frontend::NodeType::INTEGER);

    auto constraintNode = seqOfNode->getChild(1);
    assert(constraintNode->type == frontend::NodeType::CONSTRAINT);
    assert(constraintNode->name == "SizeRange");
    assert(constraintNode->getChildCount() == 2);

    auto minBound = constraintNode->getChild(0);
    assert(minBound->type == frontend::NodeType::VALUE_NODE);
    assert(minBound->value.value() == "1");
    auto maxBound = constraintNode->getChild(1);
    assert(maxBound->type == frontend::NodeType::VALUE_NODE);
    assert(maxBound->value.value() == "10");
    return true;
}

bool testParserWithEnumerated() {
    const char* source = "TestModule DEFINITIONS ::= BEGIN MyEnum ::= ENUMERATED { red(0), green, blue(5), ... } END";
    frontend::AsnLexer lexer(source, "test.asn1");
    auto tokens = lexer.tokenize();
    frontend::AsnParser parser(tokens);
    auto module = parser.parse();
    assert(module != nullptr);

    auto myEnumAssignment = module->getChild(0);
    assert(myEnumAssignment->name == "MyEnum");
    auto enumNode = myEnumAssignment->getChild(0);
    assert(enumNode->type == frontend::NodeType::ENUMERATION);
    assert(enumNode->hasExtension == true);
    assert(enumNode->getChildCount() == 4); // red, green, blue, ...

    auto red = enumNode->getChild(0);
    assert(red->name == "red");
    assert(red->value.has_value() && red->value.value() == "0");

    auto green = enumNode->getChild(1);
    assert(green->name == "green");
    assert(!green->value.has_value());

    auto blue = enumNode->getChild(2);
    assert(blue->name == "blue");
    assert(blue->value.has_value() && blue->value.value() == "5");

    assert(enumNode->getChild(3)->type == frontend::NodeType::EXTENSION_MARKER);
    return true;
}

bool testParserWithObjectIdentifier() {
    const char* source = "TestModule DEFINITIONS ::= BEGIN MyOID ::= OBJECT IDENTIFIER END";
    frontend::AsnLexer lexer(source, "test.asn1");
    auto tokens = lexer.tokenize();
    frontend::AsnParser parser(tokens);
    auto module = parser.parse();
    assert(module != nullptr);

    auto myOidAssignment = module->getChild(0);
    assert(myOidAssignment->name == "MyOID");
    auto oidNode = myOidAssignment->getChild(0);
    assert(oidNode->type == frontend::NodeType::OBJECT_IDENTIFIER);
    assert(oidNode->name == "OBJECT IDENTIFIER");

    return true;
}

bool testParserWithOctetAndBitString() {
    const char* source =
        "TestModule DEFINITIONS ::= BEGIN\n"
        "  MyOctets ::= OCTET STRING (SIZE(8))\n"
        "  MyBits ::= BIT STRING (SIZE(1..128))\n"
        "END";
    frontend::AsnLexer lexer(source, "test.asn1");
    auto tokens = lexer.tokenize();
    frontend::AsnParser parser(tokens);
    auto module = parser.parse();
    assert(module != nullptr);
    assert(module->getChildCount() == 2);

    // Test OCTET STRING
    auto octetAssignment = module->getChild(0);
    assert(octetAssignment->name == "MyOctets");
    auto osNode = octetAssignment->getChild(0);
    assert(osNode->type == frontend::NodeType::OCTET_STRING);
    assert(osNode->getChildCount() == 1);
    auto osConstraint = osNode->getChild(0);
    assert(osConstraint->type == frontend::NodeType::CONSTRAINT);
    assert(osConstraint->name == "SizeRange");
    assert(osConstraint->getChildCount() == 1);
    auto bound = osConstraint->getChild(0);
    assert(bound->type == frontend::NodeType::VALUE_NODE);
    assert(bound->value.has_value());
    assert(bound->value.value() == "8");

    // Test BIT STRING
    auto bitAssignment = module->getChild(1);
    assert(bitAssignment->name == "MyBits");
    auto bsNode = bitAssignment->getChild(0);
    assert(bsNode->type == frontend::NodeType::BIT_STRING);
    assert(bsNode->getChildCount() == 1);
    auto bsConstraint = bsNode->getChild(0);
    assert(bsConstraint->type == frontend::NodeType::CONSTRAINT);
    assert(bsConstraint->name == "SizeRange");
    assert(bsConstraint->getChildCount() == 2);
    auto minBound = bsConstraint->getChild(0);
    assert(minBound->type == frontend::NodeType::VALUE_NODE);
    assert(minBound->value.value() == "1");
    auto maxBound = bsConstraint->getChild(1);
    assert(maxBound->type == frontend::NodeType::VALUE_NODE);
    assert(maxBound->value.value() == "128");

    return true;
}

bool testParserWithDefault() {
    const char* source = "TestModule DEFINITIONS ::= BEGIN MySeq ::= SEQUENCE { val INTEGER DEFAULT 5 } END";
    frontend::AsnLexer lexer(source, "test.asn1");
    auto tokens = lexer.tokenize();
    frontend::AsnParser parser(tokens);
    auto module = parser.parse();
    assert(module != nullptr);

    auto mySeqAssignment = module->getChild(0);
    auto seqNode = mySeqAssignment->getChild(0);
    assert(seqNode->type == frontend::NodeType::SEQUENCE);
    assert(seqNode->getChildCount() == 1);

    auto member = seqNode->getChild(0);
    assert(member->name == "val");
    assert(member->hasDefault == true);
    assert(member->isOptional == false);
    assert(member->value.has_value() && member->value.value() == "5");

    return true;
}

bool testParserWithEnumDefault() {
    const char* source = 
        "TestModule DEFINITIONS ::= BEGIN\n"
        "  MyEnum ::= ENUMERATED { red, green, blue }\n"
        "  MySeq ::= SEQUENCE { color MyEnum DEFAULT green }\n"
        "END";
    frontend::AsnLexer lexer(source, "test.asn1");
    auto tokens = lexer.tokenize();
    frontend::AsnParser parser(tokens);
    auto module = parser.parse();
    assert(module != nullptr);

    auto mySeqAssignment = module->getChild(1);
    auto seqNode = mySeqAssignment->getChild(0);
    auto member = seqNode->getChild(0);

    assert(member->name == "color");
    assert(member->hasDefault == true);
    assert(member->defaultValueIsIdentifier == true);
    assert(member->value.has_value() && member->value.value() == "green");

    return true;
}

bool testParserWithNegativeDefault() {
    const char* source = "TestModule DEFINITIONS ::= BEGIN MySeq ::= SEQUENCE { val INTEGER DEFAULT -1 } END";
    frontend::AsnLexer lexer(source, "test.asn1");
    auto tokens = lexer.tokenize();
    frontend::AsnParser parser(tokens);
    auto module = parser.parse();
    assert(module != nullptr);

    auto mySeqAssignment = module->getChild(0);
    auto seqNode = mySeqAssignment->getChild(0);
    auto member = seqNode->getChild(0);

    assert(member->name == "val");
    assert(member->hasDefault == true);
    assert(member->defaultValueIsIdentifier == false);
    assert(member->value.has_value() && member->value.value() == "-1");
    return true;
}

bool testParserWithReal() {
    const char* source = "TestModule DEFINITIONS ::= BEGIN MyReal ::= REAL END";
    frontend::AsnLexer lexer(source, "test.asn1");
    auto tokens = lexer.tokenize();
    frontend::AsnParser parser(tokens);
    auto module = parser.parse();
    assert(module != nullptr);

    auto myRealAssignment = module->getChild(0);
    assert(myRealAssignment->name == "MyReal");
    auto realNode = myRealAssignment->getChild(0);
    assert(realNode->type == frontend::NodeType::REAL);
    assert(realNode->name == "REAL");

    return true;
}

bool testParserWithNull() {
    const char* source = "TestModule DEFINITIONS ::= BEGIN MyNull ::= NULL END";
    frontend::AsnLexer lexer(source, "test.asn1");
    auto tokens = lexer.tokenize();
    frontend::AsnParser parser(tokens);
    auto module = parser.parse();
    assert(module != nullptr);

    auto myNullAssignment = module->getChild(0);
    assert(myNullAssignment->name == "MyNull");
    auto nullNode = myNullAssignment->getChild(0);
    assert(nullNode->type == frontend::NodeType::NULL_TYPE);
    assert(nullNode->name == "NULL");

    return true;
}

bool testParserWithValueAssignment() {
    const char* source = 
        "TestModule DEFINITIONS ::= BEGIN\n"
        "  maxThings INTEGER ::= 1024\n"
        "  myOid OBJECT IDENTIFIER ::= { 1 2 840 113549 }\n"
        "END";
    frontend::AsnLexer lexer(source, "test.asn1");
    auto tokens = lexer.tokenize();
    frontend::AsnParser parser(tokens);
    auto module = parser.parse();
    assert(module != nullptr);
    assert(module->getChildCount() == 2);

    // Test INTEGER value assignment
    auto intValAssign = module->getChild(0);
    assert(intValAssign->type == frontend::NodeType::VALUE_ASSIGNMENT);
    assert(intValAssign->name == "maxThings");
    assert(intValAssign->getChild(0)->type == frontend::NodeType::INTEGER);
    auto intValNode = intValAssign->getChild(1);
    assert(intValNode->type == frontend::NodeType::VALUE_NODE);
    assert(intValNode->value.value() == "1024");

    // Test OID value assignment
    auto oidValAssign = module->getChild(1);
    assert(oidValAssign->type == frontend::NodeType::VALUE_ASSIGNMENT);
    assert(oidValAssign->name == "myOid");
    assert(oidValAssign->getChild(0)->type == frontend::NodeType::OBJECT_IDENTIFIER);
    auto oidValNode = oidValAssign->getChild(1);
    assert(oidValNode->type == frontend::NodeType::VALUE_NODE);
    assert(oidValNode->value.value() == "1 2 840 113549");

    return true;
}

bool testParserWithCharacterStrings() {
    const char* source =
        "TestModule DEFINITIONS ::= BEGIN\n"
        "  MyUTF8 ::= UTF8String (SIZE(1..100))\n"
        "END";
    frontend::AsnLexer lexer(source, "test.asn1");
    auto tokens = lexer.tokenize();
    frontend::AsnParser parser(tokens);
    auto module = parser.parse();
    assert(module != nullptr);

    auto assignment = module->getChild(0);
    assert(assignment->name == "MyUTF8");
    auto typeNode = assignment->getChild(0);
    assert(typeNode->type == frontend::NodeType::UTF8_STRING);
    assert(typeNode->getChildCount() == 1); // constraint
    auto constraint = typeNode->getChild(0);
    assert(constraint->name == "SizeRange");

    return true;
}

bool testParserWithChoiceExtension() {
    const char* source = "TestModule DEFINITIONS ::= BEGIN MyChoice ::= CHOICE { a INTEGER, ..., b BOOLEAN } END";
    frontend::AsnLexer lexer(source, "test.asn1");
    auto tokens = lexer.tokenize();
    frontend::AsnParser parser(tokens);
    auto module = parser.parse();
    assert(module != nullptr);
    auto choiceNode = module->getChild(0)->getChild(0);
    assert(choiceNode->type == frontend::NodeType::CHOICE);
    assert(choiceNode->hasExtension == true);
    assert(choiceNode->getChildCount() == 3);
    assert(choiceNode->getChild(1)->type == frontend::NodeType::EXTENSION_MARKER);
    return true;
}

bool testParserWithComponentsConstraint() {
    const char* source =
        "TestModule DEFINITIONS ::= BEGIN\n"
        "  MySeq ::= SEQUENCE { a INTEGER, b BOOLEAN, c REAL }\n"
        "  MySubSeq ::= MySeq (WITH COMPONENTS { a, c, ... })\n"
        "END";
    frontend::AsnLexer lexer(source, "test.asn1");
    auto tokens = lexer.tokenize();
    frontend::AsnParser parser(tokens);
    auto module = parser.parse();
    assert(module != nullptr);
    assert(module->getChildCount() == 2);

    auto subSeqAssign = module->getChild(1);
    assert(subSeqAssign->name == "MySubSeq");
    auto typeRefNode = subSeqAssign->getChild(0);
    assert(typeRefNode->type == frontend::NodeType::IDENTIFIER);
    assert(typeRefNode->name == "MySeq");
    assert(typeRefNode->getChildCount() == 1);

    auto constraintNode = typeRefNode->getChild(0);
    assert(constraintNode->type == frontend::NodeType::CONSTRAINT);
    assert(constraintNode->name == "WithComponents");
    assert(constraintNode->hasExtension == true);
    assert(constraintNode->getChildCount() == 2);
    assert(constraintNode->getChild(0)->name == "a");
    assert(constraintNode->getChild(1)->name == "c");

    return true;
}

bool testParserWithExplicitTag() {
    const char* source = "TestModule DEFINITIONS ::= BEGIN MyType ::= [APPLICATION 5] IMPLICIT INTEGER END";
    frontend::AsnLexer lexer(source, "test.asn1");
    auto tokens = lexer.tokenize();
    frontend::AsnParser parser(tokens);
    auto module = parser.parse();
    assert(module != nullptr);

    auto assignment = module->getChild(0);
    auto typeNode = assignment->getChild(0);
    assert(typeNode->type == frontend::NodeType::INTEGER);
    assert(typeNode->tag.has_value());
    
    const auto& tag = typeNode->tag.value();
    assert(tag.tag_class == frontend::AsnNode::TagClass::APPLICATION);
    assert(tag.tag_number == 5);
    assert(tag.mode == frontend::AsnNode::TaggingMode::IMPLICIT);

    return true;
}

bool testParserWithParameterizedType() {
    const char* source =
        "TestModule DEFINITIONS ::= BEGIN\n"
        "  MY-CLASS ::= CLASS { &id INTEGER, &Type TYPE }\n"
        "  mySet MY-CLASS ::= { {&id 1, &Type INTEGER} }\n"
        "  MyMessage ::= SEQUENCE {\n"
        "    id    MY-CLASS.&id({mySet}),\n"
        "    value MY-CLASS.&Type({mySet}{@id})\n"
        "  }\n"
        "END";
    frontend::AsnLexer lexer(source, "test.asn1");
    auto tokens = lexer.tokenize();
    frontend::AsnParser parser(tokens);
    auto module = parser.parse();
    assert(module != nullptr);

    auto msgAssign = module->getChild(2);
    auto seqNode = msgAssign->getChild(0);
    
    auto idMember = seqNode->getChild(0);
    auto idType = idMember->getChild(0);
    assert(idType->type == frontend::NodeType::FIELD_REFERENCE);
    assert(idType->parameters.size() == 1);
    assert(idType->parameters[0]->type == frontend::NodeType::IDENTIFIER);
    assert(idType->parameters[0]->name == "mySet");

    auto valueMember = seqNode->getChild(1);
    auto valueType = valueMember->getChild(0);
    assert(valueType->type == frontend::NodeType::FIELD_REFERENCE);
    assert(valueType->parameters.size() == 2);
    assert(valueType->parameters[0]->type == frontend::NodeType::IDENTIFIER);
    assert(valueType->parameters[0]->name == "mySet");
    assert(valueType->parameters[1]->type == frontend::NodeType::RELATIVE_REFERENCE);
    assert(valueType->parameters[1]->name == "id");

    return true;
}

bool testParserWithModuleTags() {
    const char* source = 
        "TestModule DEFINITIONS AUTOMATIC TAGS EXTENSIBILITY IMPLIED ::= BEGIN\n"
        "  MySeq ::= SEQUENCE { value INTEGER }\n"
        "END";
    frontend::AsnLexer lexer(source, "test.asn1");
    auto tokens = lexer.tokenize();
    frontend::AsnParser parser(tokens);
    auto module = parser.parse();
    assert(module != nullptr);
    assert(module->name == "TestModule");
    assert(module->tagging_environment == frontend::AsnNode::TaggingMode::AUTOMATIC);
    assert(module->extensibility_implied == true);
    assert(module->getChildCount() == 1);
    // assert(parser.peek().type == frontend::TokenType::END_OF_FILE);  // peek() is private
    return true;
}

void register_parser_tests(utils::TestFramework& runner) {
    runner.addTest("Parser: Simple SEQUENCE with members", testParser);
    runner.addTest("Parser: INTEGER with constraint", testParserWithConstraint);
    runner.addTest("Parser: ANY DEFINED BY", testParserWithAnyDefinedBy);
    runner.addTest("Parser: Value Reference in Constraint", testParserWithValueReferenceConstraint);
    runner.addTest("Parser: Type Reference", testParserWithReference);
    runner.addTest("Parser: OPTIONAL field", testParserWithOptional);
    runner.addTest("Parser: SEQUENCE with extension", testParserWithExtension);
    runner.addTest("Parser: CHOICE", testParserWithChoice);
    runner.addTest("Parser: SEQUENCE OF", testParserWithSequenceOf);
    runner.addTest("Parser: SEQUENCE OF with SIZE", testParserWithSequenceOfSizeConstraint);
    runner.addTest("Parser: ENUMERATED", testParserWithEnumerated);
    runner.addTest("Parser: OBJECT IDENTIFIER", testParserWithObjectIdentifier);
    runner.addTest("Parser: OCTET STRING and BIT STRING", testParserWithOctetAndBitString);
    runner.addTest("Parser: DEFAULT field", testParserWithDefault);
    runner.addTest("Parser: ENUMERATED DEFAULT field", testParserWithEnumDefault);
    runner.addTest("Parser: Negative DEFAULT field", testParserWithNegativeDefault);
    runner.addTest("Parser: REAL type", testParserWithReal);
    runner.addTest("Parser: NULL type", testParserWithNull);
    runner.addTest("Parser: Value Assignment", testParserWithValueAssignment);
    runner.addTest("Parser: Character String Types", testParserWithCharacterStrings);
    runner.addTest("Parser: CHOICE with extension", testParserWithChoiceExtension);
    runner.addTest("Parser: WITH COMPONENTS constraint", testParserWithComponentsConstraint);
    runner.addTest("Parser: Explicit Tagging", testParserWithExplicitTag);
    runner.addTest("Parser: Parameterized Type", testParserWithParameterizedType);
    runner.addTest("Parser: Module Tags", testParserWithModuleTags);
}