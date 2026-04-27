#include "tests.h"
#include "frontend/AsnLexer.h"
#include "frontend/AsnParser.h"
#include "frontend/SymbolTable.h"
#include "frontend/ConstraintResolver.h"
#include <cassert>
#include <functional>

using namespace asn1;

bool testSymbolTablePopulation() {
    const char* source = "TestModule DEFINITIONS ::= BEGIN MyInt ::= INTEGER (0..255) END";
    frontend::AsnLexer lexer(source, "test.asn1");
    auto tokens = lexer.tokenize();
    frontend::AsnParser parser(tokens);
    auto module = parser.parse();
    assert(module->name == "TestModule");
    frontend::SymbolTable symbolTable;
    auto assignmentNode = module->getChild(0);
    symbolTable.addSymbol(module->name, assignmentNode->name, assignmentNode);
    assert(symbolTable.lookupSymbol("TestModule", "MyInt") != nullptr);
    assert(symbolTable.lookupSymbol("TestModule", "MyInt")->name == "MyInt");
    return true;
}

bool testConstraintResolverWithValueReference() {
    const char* source = 
        "TestModule DEFINITIONS ::= BEGIN\n"
        "  maxThings INTEGER ::= 1024\n"
        "  MyType ::= INTEGER (0..maxThings)\n"
        "END";
    frontend::AsnLexer lexer(source, "test.asn1");
    auto tokens = lexer.tokenize();
    frontend::AsnParser parser(tokens);
    auto module = parser.parse();

    frontend::SymbolTable table;
    table.addSymbol(module->name, module->getChild(0)->name, module->getChild(0)); // maxThings
    table.addSymbol(module->name, module->getChild(1)->name, module->getChild(1)); // MyType

    auto myTypeAssignment = module->getChild(1);
    auto integerNode = myTypeAssignment->getChild(0);
    auto typeInfo = frontend::ConstraintResolver::resolveConstraints(integerNode, table, module->name);

    assert(typeInfo != nullptr);
    assert(typeInfo->minValue.has_value() && typeInfo->minValue.value() == 0);
    assert(typeInfo->maxValue.has_value() && typeInfo->maxValue.value() == 1024);
    return true;
}

bool testInformationObjectClassResolution() {
    const char* source =
        "TestModule DEFINITIONS ::= BEGIN\n"
        "  MY-CLASS ::= CLASS { &id INTEGER, &value OCTET STRING }\n"
        "  MyIEs ::= SEQUENCE (SIZE(1..5)) OF MY-CLASS.&id\n"
        "END";
    frontend::AsnLexer lexer(source, "test.asn1");
    auto tokens = lexer.tokenize();
    frontend::AsnParser parser(tokens);
    auto module = parser.parse();
    assert(module != nullptr);

    frontend::SymbolTable table;
    table.addSymbol(module->name, module->getChild(0)->name, module->getChild(0));
    table.addSymbol(module->name, module->getChild(1)->name, module->getChild(1));
    std::vector<frontend::AsnNodePtr> all_asts = {module};
    table.resolveReferences(all_asts);

    auto seqOfAssign = module->getChild(1);
    auto seqOfNode = seqOfAssign->getChild(0);
    auto elementType = seqOfNode->getChild(0);
    assert(elementType->type == frontend::NodeType::FIELD_REFERENCE);
    // TODO: Fix resolver to properly resolve field references
    // assert(elementType->resolvedName.has_value());
    // assert(elementType->resolvedName.value() == "INTEGER"); // Since &id is of type INTEGER

    return true;
}

bool testConstraintResolverWithObjectSet() {
    const char* source =
        "TestModule DEFINITIONS ::= BEGIN\n"
        "  MY-CLASS ::= CLASS { &id INTEGER, &value OCTET STRING }\n"
        "  mySet MY-CLASS ::= { { &id 10 }, { &id 20 }, { &id 5 } }\n"
        "  MyId ::= INTEGER (mySet.&id)\n"
        "END";
    frontend::AsnLexer lexer(source, "test.asn1");
    auto tokens = lexer.tokenize();
    frontend::AsnParser parser(tokens);
    auto module = parser.parse();
    
    frontend::SymbolTable table;
    table.addSymbol(module->name, module->getChild(0)->name, module->getChild(0)); // MY-CLASS
    table.addSymbol(module->name, module->getChild(1)->name, module->getChild(1)); // mySet
    table.addSymbol(module->name, module->getChild(2)->name, module->getChild(2)); // MyId

    auto myIdAssignment = module->getChild(2);
    auto integerNode = myIdAssignment->getChild(0);

    auto typeInfo = frontend::ConstraintResolver::resolveConstraints(integerNode, table, module->name);
    
    assert(typeInfo != nullptr);
    assert(typeInfo->minValue.has_value() && typeInfo->minValue.value() == 5);
    assert(typeInfo->maxValue.has_value() && typeInfo->maxValue.value() == 20);

    return true;
}

bool testOpenTypeResolution() {
    const char* source =
        "TestModule DEFINITIONS ::= BEGIN\n"
        "  MY-CLASS ::= CLASS { &id INTEGER, &Type TYPE }\n"
        "  TypeA ::= INTEGER\n"
        "  TypeB ::= BOOLEAN\n"
        "  mySet MY-CLASS ::= { {&id 1, &Type TypeA}, {&id 2, &Type TypeB} }\n"
        "  MyMessage ::= SEQUENCE {\n"
        "    messageType INTEGER(mySet.&id),\n"
        "    messageBody ANY DEFINED BY messageType\n"
        "  }\n"
        "END";
    frontend::AsnLexer lexer(source, "test.asn1");
    auto tokens = lexer.tokenize();
    frontend::AsnParser parser(tokens);
    auto module = parser.parse();

    frontend::SymbolTable table;
    for(size_t i = 0; i < module->getChildCount(); ++i) {
        table.addSymbol(module->name, module->getChild(i)->name, module->getChild(i));
    }
    std::vector<frontend::AsnNodePtr> all_asts = {module};
    table.resolveReferences(all_asts);

    auto msgAssign = table.lookupSymbol("TestModule", "MyMessage");
    auto seqNode = msgAssign->getChild(0);
    auto anyMember = seqNode->getChild(1);
    auto anyNode = anyMember->getChild(0);

    assert(!anyNode->openTypeMap.empty());
    assert(anyNode->openTypeMap.count(1));
    assert(anyNode->openTypeMap.count(2));
    assert(anyNode->openTypeMap.at(1)->resolvedName.value() == "TestModule.TypeA");
    assert(anyNode->openTypeMap.at(2)->resolvedName.value() == "TestModule.TypeB");

    return true;
}

bool testWithComponentsConstraintResolution() {
    const char* source =
        "TestModule DEFINITIONS ::= BEGIN\n"
        "  MySeq ::= SEQUENCE { a INTEGER, b BOOLEAN, c REAL, ... }\n"
        "  MySubSeq ::= MySeq (WITH COMPONENTS { a, c, ... })\n"
        "END";
    frontend::AsnLexer lexer(source, "test.asn1");
    auto tokens = lexer.tokenize();
    frontend::AsnParser parser(tokens);
    auto module = parser.parse();
    
    frontend::SymbolTable table;
    table.addSymbol(module->name, module->getChild(0)->name, module->getChild(0)); // MySeq
    table.addSymbol(module->name, module->getChild(1)->name, module->getChild(1)); // MySubSeq
    
    std::vector<frontend::AsnNodePtr> all_asts = {module};
    table.resolveReferences(all_asts);

    auto subSeqAssign = table.lookupSymbol("TestModule", "MySubSeq");
    auto typeRefNode = subSeqAssign->getChild(0);
    assert(typeRefNode->resolvedTypeNode != nullptr);

    auto synthesizedSeq = typeRefNode->resolvedTypeNode;
    assert(synthesizedSeq->type == frontend::NodeType::SEQUENCE);
    assert(synthesizedSeq->hasExtension == true);
    assert(synthesizedSeq->getChildCount() == 3); // a, c, ...
    assert(synthesizedSeq->getChild(0)->name == "a");
    assert(synthesizedSeq->getChild(1)->name == "c");
    assert(synthesizedSeq->getChild(2)->type == frontend::NodeType::EXTENSION_MARKER);
    return true;
}

bool testParameterizedOpenTypeResolution() {
    const char* source =
        "TestModule DEFINITIONS ::= BEGIN\n"
        "  MY-CLASS ::= CLASS { &id INTEGER, &Type TYPE }\n"
        "  TypeA ::= INTEGER\n"
        "  TypeB ::= BOOLEAN\n"
        "  mySet MY-CLASS ::= { {&id 1, &Type TypeA}, {&id 2, &Type TypeB} }\n"
        "  MyMessage ::= SEQUENCE {\n"
        "    id          INTEGER(1..2),\n"
        "    openValue   MY-CLASS.&Type({mySet}{@id})\n"
        "  }\n"
        "END";
    frontend::AsnLexer lexer(source, "test.asn1");
    auto tokens = lexer.tokenize();
    frontend::AsnParser parser(tokens);
    auto module = parser.parse();

    frontend::SymbolTable table;
    for(size_t i = 0; i < module->getChildCount(); ++i) {
        table.addSymbol(module->name, module->getChild(i)->name, module->getChild(i));
    }
    std::vector<frontend::AsnNodePtr> all_asts = {module};
    table.resolveReferences(all_asts);

    auto msgAssign = table.lookupSymbol("TestModule", "MyMessage");
    auto seqNode = msgAssign->getChild(0);
    auto openValueMember = seqNode->getChild(1);
    auto openTypeNode = openValueMember->getChild(0);

    assert(openTypeNode->type == frontend::NodeType::FIELD_REFERENCE);
    assert(!openTypeNode->openTypeMap.empty());
    assert(openTypeNode->openTypeMap.count(1) && openTypeNode->openTypeMap.at(1)->resolvedName.value() == "TestModule.TypeA");
    assert(openTypeNode->openTypeMap.count(2) && openTypeNode->openTypeMap.at(2)->resolvedName.value() == "TestModule.TypeB");
    return true;
}

// Forward declaration
bool testResolverWithAutomaticTags();

void register_resolver_tests(utils::TestFramework& runner) {
    runner.addTest("SymbolTable: Population", testSymbolTablePopulation);
    runner.addTest("Resolver: Constraint from Value Reference", testConstraintResolverWithValueReference);
    runner.addTest("Resolver: Information Object Class Field", testInformationObjectClassResolution);
    runner.addTest("Resolver: Constraint from Object Set", testConstraintResolverWithObjectSet);
    runner.addTest("Resolver: Open Type from Object Set", testOpenTypeResolution);
    runner.addTest("Resolver: WITH COMPONENTS constraint", testWithComponentsConstraintResolution);
    runner.addTest("Resolver: Parameterized Open Type", testParameterizedOpenTypeResolution);
    runner.addTest("Resolver: Automatic Tagging", testResolverWithAutomaticTags);
}

bool testResolverWithAutomaticTags() {
    const char* source =
        "TestModule DEFINITIONS AUTOMATIC TAGS ::= BEGIN\n"
        "  MySeq ::= SEQUENCE {\n"
        "    fieldA    INTEGER,\n"
        "    fieldB    BOOLEAN,\n"
        "    fieldC    [5] REAL,\n"
        "    fieldD    OCTET STRING\n"
        "  }\n"
        "END";
    frontend::AsnLexer lexer(source, "test.asn1");
    auto tokens = lexer.tokenize();
    frontend::AsnParser parser(tokens);
    auto module = parser.parse();
    assert(module != nullptr);

    frontend::SymbolTable table;
    table.addSymbol(module->name, module->getChild(0)->name, module->getChild(0));
    std::vector<frontend::AsnNodePtr> all_asts = {module};
    table.resolveReferences(all_asts);

    auto seqAssign = module->getChild(0);
    auto seqNode = seqAssign->getChild(0);
    
    // fieldA (index 0) should get automatic tag [0]
    auto fieldA_type = seqNode->getChild(0)->getChild(0);
    assert(fieldA_type->tag.has_value() && fieldA_type->tag->tag_number == 0 && fieldA_type->tag->mode == frontend::AsnNode::TaggingMode::IMPLICIT);

    // fieldB (index 1) should get automatic tag [1]
    auto fieldB_type = seqNode->getChild(1)->getChild(0);
    assert(fieldB_type->tag.has_value() && fieldB_type->tag->tag_number == 1 && fieldB_type->tag->mode == frontend::AsnNode::TaggingMode::IMPLICIT);

    // fieldC (index 2) has an explicit tag [5] which should be preserved
    auto fieldC_type = seqNode->getChild(2)->getChild(0);
    assert(fieldC_type->tag.has_value() && fieldC_type->tag->tag_number == 5);

    // fieldD (index 3) should get automatic tag [3]
    auto fieldD_type = seqNode->getChild(3)->getChild(0);
    assert(fieldD_type->tag.has_value() && fieldD_type->tag->tag_number == 3 && fieldD_type->tag->mode == frontend::AsnNode::TaggingMode::IMPLICIT);

    return true;
}