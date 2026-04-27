# ASN.1 Compiler — Implementation Reference

This document describes how the compiler is built: what each component does,
the data structures it uses, and the algorithms that connect them. It is
intended as an on-boarding guide for contributors and as a reference when
adding new features.

---

## Table of Contents

1. [High-Level Pipeline](#1-high-level-pipeline)
2. [Frontend: Lexer](#2-frontend-lexer)
3. [Frontend: Parser](#3-frontend-parser)
4. [Frontend: Symbol Table & Resolver](#4-frontend-symbol-table--resolver)
5. [Frontend: Constraint Resolver](#5-frontend-constraint-resolver)
6. [Runtime: Core (BitStream)](#6-runtime-core-bitstream)
7. [Runtime: UPER Codecs](#7-runtime-uper-codecs)
8. [Codegen: TypeMap & Formatter](#8-codegen-typemap--formatter)
9. [Codegen: CppEmitter](#9-codegen-cppemitter)
10. [Codegen: CodecEmitter](#10-codegen-codecemitter)
11. [Utils: CLI Driver](#11-utils-cli-driver)
12. [Key Data Structures](#12-key-data-structures)
13. [ASN.1 Features Supported](#13-asn1-features-supported)
14. [Known Limitations](#14-known-limitations)

---

## 1. High-Level Pipeline

```
.asn1 file  (may contain multiple DEFINITIONS … BEGIN … END blocks)
    │
    ▼
AsnLexer          src/frontend/AsnLexer.cpp
    │  produces vector<Token>
    ▼
AsnParser         src/frontend/AsnParser.cpp
    │  parseAll() → vector<AsnNode> (one MODULE root per block)
    ▼
SymbolTable       src/frontend/SymbolTable.cpp
    │  all modules registered; resolves cross-references, open types, tagging
    ▼
  ┌─ for each module ──────────────────────────────────────┐
  │ CppEmitter      src/codegen/cpp/CppEmitter.cpp         │
  │   traverses resolved AST → .h types inside namespace   │
  │ CodecEmitter    src/codegen/cpp/CodecEmitter.cpp        │
  │   traverses resolved AST → .cpp encode_/decode_ funcs  │
  └────────────────────────────────────────────────────────┘
    │
    ▼
.h + .cpp files  (one namespace per module)
```

The `CompilerMain.cpp` entry point wires these stages together, driven by the
`FileLoader` and `Logger` utilities.

---

## 2. Frontend: Lexer

**Files:** `src/frontend/AsnLexer.cpp`, `include/frontend/AsnLexer.h`,
`include/frontend/Token.h`, `src/frontend/Token.cpp`

### Token types

All token types are defined in `Token.h` as the `TokenType` enum. Notable groups:

- **Structural:** `LBRACE`, `RBRACE`, `LPAREN`, `RPAREN`, `LBRACKET`, `RBRACKET`,
  `COMMA`, `SEMICOLON`, `DOT`, `DOTDOT`, `ELLIPSIS`, `ASSIGNMENT` (`::=`),
  `PIPE`, `AT_SIGN`, `AMPERSAND`
- **Literals:** `NUMBER`, `REAL_NUMBER`, `STRING`, `IDENTIFIER`
- **Keywords:** one token type per ASN.1 reserved word — `SEQUENCE`, `CHOICE`,
  `OPTIONAL`, `CLASS`, `WITH`, `COMPONENTS`, `IMPLICIT`, `EXPLICIT`,
  `AUTOMATIC`, `TAGS`, `DEFINITIONS`, `BEGIN`, `END`, `IMPORTS`, `FROM`, etc.

### Scanning strategy

`AsnLexer::tokenize()` calls `getNextToken()` in a loop until `END_OF_FILE`.

`getNextToken()`:
1. Skips whitespace (`skipWhitespace()`).
2. Skips `-- comment` lines (`skipComment()`, called recursively).
3. Dispatches on the first character:
   - Alpha/underscore → `scanIdentifier()`: reads an alphanumeric run, then
     checks a static keyword map. If found, returns the keyword token; otherwise
     returns `IDENTIFIER`.
   - Digit → `scanNumber()`: reads digits; if a `.` follows, reads a real number.
   - `"` or `'` → `scanString()`.
   - Everything else → `scanOperator()`: handles multi-character operators
     (`..`, `...`, `::=`, `@`, `&`).

`SourceLocation` (line, column, filename) is attached to every token for
diagnostic messages.

---

## 3. Frontend: Parser

**Files:** `src/frontend/AsnParser.cpp`, `include/frontend/AsnParser.h`,
`src/frontend/AsnNode.cpp`, `include/frontend/AsnNode.h`,
`src/frontend/ParseUtils.cpp`

### Recursive-descent design

The parser is a hand-written recursive-descent parser with one-token lookahead
(`peek()` / `peekNext()`). Every grammar production is a `parseXxx()` method
returning an `AsnNodePtr` (shared_ptr to `AsnNode`).

Key productions and the AST shape they build:

#### Multi-module files

A single `.asn1` file may contain multiple consecutive `ModuleName DEFINITIONS … BEGIN … END` blocks (as in 3GPP NR-RRC, which has `NR-RRC-Definitions`, `NR-UE-Variables`, and `NR-InterNodeDefinitions` in one file).

`AsnParser::parse()` reads exactly one module. `AsnParser::parseAll()` loops calling `parseModuleDefinition()` until `END_OF_FILE`, returning a `vector<AsnNodePtr>` — one element per module. The CLI uses `parseAll()` so every block in the file is processed.

#### Module

```
parseModuleDefinition()
  → AsnNode(MODULE, "<ModuleName>")
      children: IMPORTS node, then zero or more ASSIGNMENT nodes
```

The module node records `tagging_environment` (EXPLICIT / IMPLICIT / AUTOMATIC)
and `extensibility_implied` from the `DEFINITIONS` header.

#### Assignment

```
parseAssignment()
  handles:
    TypeName ::= TypeDefinition         → ASSIGNMENT("TypeName") [type node]
    valueName Type ::= { ... }          → ASSIGNMENT("valueName") [class-ref, object-set]
    TypeName { Params } ::= ...         → ASSIGNMENT (isParameterized=true, parameters=[...])
    TypeName ::= CLASS { ... }          → ASSIGNMENT [CLASS_DEFINITION [...]]
```

Object set assignments (lower-case names followed by a class reference and
`::= { ... }`) are detected by checking that the name starts with a lower-case
letter and a `{` follows `::=`. The parser calls `parseObjectSet()` which
reads `{ objectDef, objectDef, ... }` into `OBJECT_DEFINITION` children.

Parameterized assignments are detected by a `{` immediately after the name,
which triggers `parseParameterList()`. Parameters are stored in
`AsnNode::parameters`.

#### Type

```
parseType()
  dispatches on current token:
    [n]               → tag prefix → recursive parseType() with tag attached
    SEQUENCE          → parseSequence()
    SEQUENCE OF       → parseSequenceOf()
    CHOICE            → parseChoice()
    INTEGER/BOOLEAN/… → leaf node for primitive
    IDENTIFIER        → check scope: FIELD_REFERENCE if & prefix, else IDENTIFIER node
                        followed by optional constraint or parameter list
```

Tag prefixes (`[0]`, `[APPLICATION 1] EXPLICIT`, etc.) are parsed at the top of
`parseType()`. They create a tagged wrapper by annotating the inner type node's
`tag` field (`AsnNode::AsnTag`).

#### Constraint

```
parseConstraint()   (called after LPAREN)
  handles:
    n..m              → CONSTRAINT("Range")      children: [min, max]
    SIZE(n..m)        → CONSTRAINT("Size")       children: [inner constraint]
    n                 → CONSTRAINT("SingleValue")
    FROM { ... }      → CONSTRAINT("From")
    WITH COMPONENTS { id, id, ... }
                      → CONSTRAINT("WithComponents") children: [IDENTIFIER, ...]
    CONTAINING Type   → CONSTRAINT("Containing")
    a | b             → CONSTRAINT("Union")
    IDENTIFIER        → CONSTRAINT("ValueRef")   (value reference)
    object.&field     → CONSTRAINT("TableConstraint") children: [FIELD_REFERENCE]
```

`WITH COMPONENTS` records the named component list in children, plus
`hasExtension` if `...` appears inside the braces.

---

## 4. Frontend: Symbol Table & Resolver

**Files:** `src/frontend/SymbolTable.cpp`, `include/frontend/SymbolTable.h`

### Symbol table structure

```cpp
class SymbolTable {
    // modules["ModuleName"]["SymbolName"] = ASSIGNMENT node ptr
    std::map<std::string, std::map<std::string, AsnNodePtr>> modules;
};
```

`addSymbol(moduleName, symbolName, assignmentNode)` inserts an entry.
`lookupSymbol(moduleName, symbolName)` retrieves it.

### Resolution pass

`resolveReferences(vector<AsnNodePtr> all_asts)` runs once after all symbols
are added. For each module it:

1. Builds a `scope` map: `symbolName → "ModuleName.symbolName"` for all
   local symbols plus all imported symbols.
2. Calls `resolveNodeReferences(node, table, scope, moduleNode, resolution_path)`
   recursively on every assignment node.
3. Runs the **automatic tagging post-pass** for modules declared with
   `AUTOMATIC TAGS`.

### `resolveNodeReferences` — key cases

**`IDENTIFIER` node** (type reference):
- **Early-exit guard**: if `node->resolvedName` is already set (resolved in an earlier module's pass), return immediately. This is essential for multi-module files: when module B imports a type from module A and the resolver follows that type into A's AST, the nodes inside A were already resolved using A's scope. Re-resolving them with B's (narrower) scope would fail for any type that B didn't explicitly import.
- Check primitives set (`INTEGER`, `BOOLEAN`, …, `TYPE`). If primitive, skip.
- Otherwise look up in `scope`. Set `node->resolvedName = qualifiedName`.
- Look up the `ASSIGNMENT` for the qualified name. If the target is not
  parameterized, recursively resolve it (with cycle detection via
  `resolution_path`).
- If the target *is* parameterized: substitute `node->parameters[i]` for each
  `assignmentNode->parameters[i]` in a deep copy of the type body, then recurse
  on the substituted copy and set `node->resolvedTypeNode` to it.
- **WITH COMPONENTS synthesis**: after setting `resolvedTypeNode`, scan the
  node's children for a `CONSTRAINT("WithComponents")` node. If found, build a
  new `SEQUENCE` containing only the named members (plus `...` if
  `hasExtension` is set), and replace `resolvedTypeNode` with the synthesised
  sequence.

**`FIELD_REFERENCE` node** (`MY-CLASS.&field` or `mySet.&field`):
- **Early-exit guard**: same as IDENTIFIER — if `resolvedName` is already set, return.
- Look up the left-hand side in scope. If its `ASSIGNMENT->child(0)` is a
  `CLASS_DEFINITION`, use it directly.
- If it is an `IDENTIFIER` node, treat the LHS as an object set: look up the
  IDENTIFIER as a class name and use *that* class's `CLASS_DEFINITION`.
- Find the named field spec inside the class. Record `node->resolvedTypeNode`.
- Resolve the field's type: if the type name is a primitive keyword (including
  `TYPE`), record it literally. Otherwise look it up in scope.

**`SEQUENCE` node** (open-type resolution):
- For each member of the sequence, look for one of two patterns:
  - `ANY DEFINED BY <name>`: locate the sibling field named `<name>`, find its
    `TableConstraint` child, then call `buildOpenTypeMap()`.
  - `FIELD_REFERENCE` with non-empty `parameters`: extract the object set name
    and `@fieldName` relative reference from parameters, then call
    `buildOpenTypeMap()`.

**`buildOpenTypeMap()`**:
1. Looks up the object set assignment by name.
2. Finds the class definition from the object set's class reference.
3. Finds the `TYPE`-typed field within the class (the open type slot).
4. Iterates over every object definition in the set, pairing `&id` values with
   `&Type` nodes.
5. Stores `id → typeNode` in `openTypeNode->openTypeMap`.

### IMPORTS node — recursion guard

The "Recurse on children" block at the bottom of `resolveNodeReferences` skips all children when `node->type == NodeType::IMPORTS`. IMPORTS children are plain name strings used only to build the scope map; treating them as type expressions would erroneously attempt to resolve parameterized types (e.g., `SetupRelease`) without their required parameter list.

### Automatic tagging post-pass

After resolution, for modules with `AUTOMATIC TAGS`:
- Walk every `SEQUENCE`/`SET` assignment.
- For members that have no explicit tag on their type, assign
  `AsnTag{CONTEXT_SPECIFIC, tagIndex++, IMPLICIT}` sequentially.
- Extension markers reset nothing; they are skipped.

---

## 5. Frontend: Constraint Resolver

**Files:** `src/frontend/ConstraintResolver.cpp`,
`include/frontend/ConstraintResolver.h`, `include/frontend/AsnTypeInfo.h`

`ConstraintResolver::resolveConstraints(typeNode, table, moduleName)` returns
an `AsnTypeInfo` describing the min/max integer range, bit width, and size
constraints for a type. It is used by the codec emitter to determine encoding
parameters.

The resolver walks the constraint children of a type node:
- `CONSTRAINT("Range")`: sets `minValue` / `maxValue` directly.
- `CONSTRAINT("ValueRef")`: looks up the symbol in the symbol table to get its
  integer value, then sets `minValue` or `maxValue`. When the constant is
  imported from another module, `resolveBound` first tries the current module,
  then falls back to the module recorded in `boundNode->resolvedName` (set by
  the resolver pass). This handles cross-module value references such as
  `SEQUENCE (SIZE (1..maxNrofCellMeas))` where `maxNrofCellMeas` is defined in
  `NR-RRC-Definitions` but used in `NR-UE-Variables`.
- `CONSTRAINT("TableConstraint")`: extracts the min/max from the referenced
  object set's `&id` field values (used for `INTEGER(mySet.&id)`).
- `CONSTRAINT("Size")`: sets `minSize` / `maxSize`.

`AsnTypeInfo::calculateBitWidth()` computes the number of bits needed to
represent a constrained integer range: `ceil(log2(max - min + 1))`.

---

## 6. Runtime: Core (BitStream)

**Files:** `src/runtime/core/`, `include/runtime/core/`

### BitWriter

`BitWriter` accumulates bits into an internal `vector<uint8_t>` buffer.

- `writeBit(bool)`: appends one bit to the current byte, flushing to the buffer
  when a full byte is complete.
- `writeBits(uint64_t value, int n)`: writes the `n` most-significant bits of
  `value`.
- `writeAligned(uint8_t*, n)`: byte-aligns then copies `n` bytes (used for
  OCTET STRING).
- `align()`: pads to the next byte boundary with zeros.
- `data()`: returns the completed buffer.

### BitReader

`BitReader` reads from a `const uint8_t*` / size pair.

- `readBit()`: returns the next bit.
- `readBits(int n)`: reads `n` bits into a `uint64_t`.
- `readAligned(n)`: byte-aligns then copies `n` bytes.
- `align()`: advances to the next byte boundary.

### BitUtils

Stateless helpers: `computeMinBits(range)`, `bitsNeeded(value)`, mask
operations used by the UPER integer codec.

### Supporting types

- `BitString.h`: wrapper holding a bit buffer and bit length; used for
  ASN.1 BIT STRING members in generated structs.
- `ExtensionValue.h`: `std::any`-based container for open-type values.
- `ObjectIdentifier.h`: thin wrapper around `vector<uint32_t>` for OID arcs.

---

## 7. Runtime: UPER Codecs

**Files:** `src/runtime/uper/`, `include/runtime/uper/`

Each file provides `encode_*` and `decode_*` free functions that operate on
`BitWriter` / `BitReader`.

### UperInteger

- `encodeConstrainedWholeNumber(bw, value, min, max)`: computes range =
  max − min, writes `value − min` in `ceil(log2(range+1))` bits.
- `encodeUnconstrainedWholeNumber(bw, value)`: length-determinant prefix +
  2's-complement bytes.
- `encodeSemiConstrainedWholeNumber(bw, value, min)`: value − min in
  unconstrained form.

### UperLength

- `encodeLength(bw, length)`: handles the three-tier UPER length encoding:
  - 0–127: one byte with bit 7 = 0.
  - 128–16383: two bytes with bits 15:14 = `10`.
  - >16383: fragmentation — emits `0xC0 | fragments` prefix, then the fragment.
- `decodeLength(br)`: reverse of above; fragmentation returns partial lengths
  that the caller must loop over.

### UperChoice

- `encodeChoiceIndex(bw, index, count)`: encodes the variant index as a
  normally-small number if extensible, otherwise as a constrained integer.
- `decodeChoiceIndex(br, count)`: reverse.

### UperSequence

- `encodeOptionalBitmap(bw, bits)`: writes a bitmap of OPTIONAL present/absent
  bits at the start of a SEQUENCE.
- `decodeOptionalBitmap(br, count)`: reads and returns the bitmap.

### UperExtension

- `encodeExtensionBit(bw, hasExtension)`: writes the extension bit at the start
  of extensible types.
- `decodeExtensionBit(br)`: reads the extension bit.
- Open-type encoding wraps the payload with a length prefix.

### UperObjectIdentifier / UperReal

Encode/decode OID arc lists and IEEE 754 REAL values respectively.

### RangeUtils

`extractRange(constraintNode)` inspects an ASN.1 constraint node and returns
`{min, max}` as `optional<long long>` pair. Used by `CodecEmitter` at
code-generation time (not at runtime).

---

## 8. Codegen: TypeMap & Formatter

**Files:** `src/codegen/TypeMap.cpp`, `include/codegen/TypeMap.h`,
`src/codegen/Formatter.cpp`

### TypeMap

Maps ASN.1 built-in types to C++ type strings:

| ASN.1 | C++ |
|---|---|
| INTEGER | `int64_t` (unconstrained) or `uint8_t`/`uint16_t`/`uint32_t` (constrained) |
| BOOLEAN | `bool` |
| OCTET STRING | `std::vector<uint8_t>` |
| BIT STRING | `asn1::core::BitString` |
| NULL | `std::nullptr_t` |
| REAL | `double` |
| OBJECT IDENTIFIER | `asn1::core::ObjectIdentifier` |
| UTF8String / PrintableString / … | `std::string` |
| ENUMERATED | `enum class` (emitted inline) |
| SEQUENCE | `struct` |
| SEQUENCE OF / SET OF | `std::vector<ElementType>` |
| CHOICE | `std::variant<A, B, …>` |

`TypeMap::mangleName(name)` converts hyphens and dots to underscores to produce
valid C++ identifiers.

`TypeMap::resolvedNameToCppRef(qualifiedName, table)` looks up the qualified
name in the symbol table and returns the mangled name of the resolved type, used
when an element type is a reference rather than an inline definition.

### Formatter

`Formatter::formatCode(code)` is a pass-through today (no re-indentation). It
exists as an extension point for pretty-printing or wrapping long lines.

`TemplateManager` provides simple string substitution into code templates, used
for boilerplate (function signatures, namespace wrappers).

---

## 9. Codegen: CppEmitter

**Files:** `src/codegen/cpp/CppEmitter.cpp`, `include/codegen/cpp/CppEmitter.h`

`CppEmitter` produces the `.h` file. It is called once per top-level assignment
in the module.

### Entry points

```cpp
std::string emitHeaderPreamble(headerGuard)     // #ifndef guard + includes
std::string emitStruct(assignmentNode, module)  // SEQUENCE → C++ struct
std::string emitChoice(assignmentNode, module)  // CHOICE → std::variant typedef
std::string emitSequenceOf(assignmentNode, module) // SEQUENCE OF → vector typedef
std::string emitEnum(assignmentNode, module)    // ENUMERATED → enum class
std::string emitPrimitive(assignmentNode, module)  // primitive typedef
```

### `emitStruct` — two-pass approach

**First pass** (`emitForwardDeclarations`): walks the member list and emits any
inline nested types that must appear *before* the struct:
- Inline `CHOICE` member type → emits a `std::variant` typedef named
  `<memberName>_type`.
- Inline `SEQUENCE` member type → emits a forward `struct <memberName>_type`.
- Inline `SEQUENCE_OF` or `SET_OF` member whose element is an inline `CHOICE`
  or `SEQUENCE` → emits the element type first, then a `vector<>` alias.

This ordering ensures the struct body can reference all nested types by name.

**Second pass**: emits the struct body, mapping each member's effective type
to a C++ field declaration. The "effective type" is found by:
1. If the member has a `resolvedTypeNode`, use it.
2. Otherwise use the direct child type node.

For optional members, the field is wrapped in `std::optional<>`.
For members with a default value, the field is given a C++ default initialiser.

Extension members (after `...`) are wrapped in `std::optional<>` by convention.

### `emitChoice` — `std::variant`

Iterates the CHOICE alternatives and builds a comma-separated list of C++
type strings for the `std::variant<…>` instantiation. Alternatives that are
themselves inline types are recursively emitted before the variant typedef.

### `emitSequenceOf` — element type resolution

1. If the element node has `resolvedName`, use `TypeMap::resolvedNameToCppRef`.
2. If the element is an inline `CHOICE`, emit a named `_element` alias via
   `emitChoice`, then use that name.
3. If the element is an inline `SEQUENCE`, emit a nested struct via `emitStruct`,
   then use that name.
4. Otherwise fall back to `TypeMap::mapAsnToCppType`. If that returns a
   structural type keyword (`struct`, `enum`, `std::variant`, `std::vector`),
   substitute `uint8_t` as a safe fallback.

The final line is `using <MangledName> = std::vector<<ElementTypeName>>;`.

---

## 10. Codegen: CodecEmitter

**Files:** `src/codegen/cpp/CodecEmitter.cpp`,
`include/codegen/cpp/CodecEmitter.h`

`CodecEmitter` produces the `.cpp` file. For each top-level assignment it emits
a pair `encode_<Name>` / `decode_<Name>` functions.

### Dispatch

`emitEncoderDefinition` / `emitDecoderDefinition` inspect `typeNode->type`:

| Node type | Generator |
|---|---|
| `SEQUENCE` | `generateSequenceLogic` |
| `SEQUENCE_OF` / `SET_OF` | `generateSequenceOfLogic` |
| `CHOICE` | `generateChoiceLogic` |
| `ENUMERATED` | `generateEnumeratedLogic` |
| `INTEGER` | `generateIntegerLogic` (constrained or unconstrained) |
| `OCTET_STRING` | length + bytes |
| `BIT_STRING` | length + bits |
| `BOOLEAN` | single bit |
| `OBJECT_IDENTIFIER` | `UperObjectIdentifier` |
| `REAL` | `UperReal` |
| `NULL_TYPE` | no-op |
| `IDENTIFIER` (reference) | delegate to `encode_<ResolvedName>` |

### `generateSequenceLogic`

**Encoder:**
1. Emit `encodeExtensionBit(writer, …)` if `hasExtension`.
2. Emit `encodeOptionalBitmap(writer, …)` for each OPTIONAL member.
3. For each non-extension member, emit `generateMemberCodecCall(member, …)`.
4. For extension members, emit open-type wrapping.

**Decoder:**
1. Emit `decodeExtensionBit(reader)`.
2. Emit `decodeOptionalBitmap(reader, count)`.
3. For each member, conditional on the optional bitmap bit, emit the decode call.

### `generateChoiceLogic`

**Encoder:**
```cpp
encodeChoiceIndex(writer, value.index(), N);
switch (value.index()) {
  case 0: encode_Alt0(writer, std::get<Alt0Type>(value)); break;
  ...
}
```
The variant alternative's field in `std::get<>` uses `safeId()` to escape C++
keywords (e.g., `explicit` → `explicit_`).

**Decoder:**
```cpp
int idx = decodeChoiceIndex(reader, N);
switch (idx) {
  case 0: { Alt0Type v; decode_Alt0(reader, v); return v; } break;
  ...
}
```

### `generateSequenceOfLogic`

**Encoder:**
```cpp
encodeLength(writer, value.size());
for (const auto& item : value) { encode_Element(writer, item); }
```

**Decoder:**
```cpp
auto len = decodeLength(reader);
for (size_t i = 0; i < len; i++) { result.push_back(decode_Element(reader)); }
```

### `generateMemberCodecCall`

This helper resolves the codec function name for a single member:
1. If the member's type node has `resolvedName`, use `encode_<ResolvedMangledName>`.
2. Otherwise dispatch on `NodeType` to inline the primitive codec call.
3. For OPTIONAL members, wrap in `if (value.<field>.has_value())` (encoder) or
   assign to `result.<field>` only when the optional bitmap bit is set (decoder).

### Cycle / recursion detection

Both emitters maintain a `recursion_depth` counter and a `processingNodes`
set keyed on type names. Self-referential types (linked lists, trees) are
detected and generate a forward declaration + pointer-based field rather than
infinite recursion.

---

## 11. Utils: CLI Driver

**Files:** `src/utils/CompilerMain.cpp`, `src/utils/FileLoader.cpp`,
`src/utils/Logger.cpp`

`CompilerMain.cpp::main()`:
1. Parses arguments: `<input.asn1>`, `-o <prefix>`, `-v`.
2. `FileLoader::loadFile(path)` → `std::string source`.
3. Lex → `AsnLexer(source).tokenize()`.
4. Parse → `AsnParser(tokens).parseAll()` → `vector<AsnNodePtr>`, one element
   per `DEFINITIONS … BEGIN … END` block in the file.
5. For each module AST: register all its ASSIGNMENT symbols in the global
   `SymbolTable`; queue any imported module file paths for subsequent parsing
   (multi-file IMPORTS support).
6. Call `globalSymbolTable.resolveReferences(all_asts)` — resolves all modules
   in one pass so cross-module references work.
7. Emit shared `#ifndef` guard and outer namespace open.
8. For each module AST: open a nested `namespace <ModuleName>`, topologically
   sort its assignments, call `CppEmitter` and `CodecEmitter` for each
   assignment, close the namespace. Each module's types and codec functions are
   thus isolated in their own namespace.
9. `FileLoader::writeFile(prefix + ".h", header)`,
   `FileLoader::writeFile(prefix + ".cpp", source)`.

`Logger` provides `Logger::debug(msg)`, `Logger::info(msg)`, `Logger::error(msg)`
with compile-time level filtering. Verbose mode (`-v`) enables DEBUG level.

---

## 12. Key Data Structures

### `AsnNode`

The universal AST node. Every parser production returns or populates one.

```
AsnNode {
  NodeType type           // what kind of node
  string   name           // type name, member name, keyword string, etc.
  SourceLocation location // file:line:col for diagnostics
  vector<AsnNodePtr> children    // sub-nodes (members, constraints, …)
  vector<AsnNodePtr> parameters  // for parameterized types
  optional<string> value         // literal value (number, string, field name)
  optional<string> resolvedName  // set by resolver: "Module.SymbolName"
  AsnNodePtr resolvedTypeNode    // points to the resolved type's AST subtree
  optional<string> definingFieldName  // for open types: sibling field name
  map<long long, AsnNodePtr> openTypeMap  // id → type, for ANY DEFINED BY
  bool isOptional / hasDefault / isParameterized / hasExtension / isTypeField
  optional<AsnTag> tag       // tag class + number + mode
  TaggingMode tagging_environment  // module-level default
}
```

Children layout varies by node type:
- `ASSIGNMENT`: child(0) = type definition; child(1) = object set body (if any).
- `SEQUENCE`: children = member ASSIGNMENT nodes.
- `CHOICE`: children = alternative ASSIGNMENT nodes.
- `SEQUENCE_OF`: child(0) = element type node; child(1) = SIZE constraint (if any).
- `CONSTRAINT`: children depend on constraint kind.
- `CLASS_DEFINITION`: children = field spec ASSIGNMENT nodes.

### `AsnTypeInfo`

Result of `ConstraintResolver`. Used by `CodecEmitter` to select the right
UPER encoding call.

```
AsnTypeInfo {
  string typeName
  optional<long long> minValue, maxValue   // integer range
  optional<int> minSize, maxSize           // size constraint
  optional<int> minBits, maxBits           // computed bit width
  bool isFixedSize, hasExtension
}
```

---

## 13. ASN.1 Features Supported

| Feature | Status |
|---|---|
| SEQUENCE, SET | Supported |
| SEQUENCE OF, SET OF | Supported |
| CHOICE | Supported |
| ENUMERATED | Supported |
| INTEGER (constrained + unconstrained) | Supported |
| BOOLEAN | Supported |
| OCTET STRING | Supported |
| BIT STRING | Supported |
| OBJECT IDENTIFIER | Supported |
| REAL | Supported |
| NULL | Supported |
| UTF8String / PrintableString / VisibleString / IA5String / NumericString | Supported (as `std::string`) |
| OPTIONAL members | Supported |
| DEFAULT values | Supported (field initializer) |
| Extension markers (`...`) | Supported (extension bit) |
| Constraints: range (`0..255`) | Supported |
| Constraints: SIZE | Supported |
| Constraints: value reference | Supported |
| Constraints: WITH COMPONENTS | Supported (synthesised filtered SEQUENCE) |
| Constraints: CONTAINING | Parsed, not yet used in codegen |
| Multiple modules in one file | Supported — each gets its own C++ namespace |
| IMPORTS within the same file | Supported — cross-module references resolved |
| IMPORTS across separate files | CLI queues imported files; partial support |
| Value assignments (`x INTEGER ::= 42`) | Supported |
| CLASS definitions + object sets | Parsed + resolved |
| Open types (ANY DEFINED BY) | Resolved (openTypeMap populated) |
| Parameterized open types (`CLASS.&Type({set}{@id})`) | Resolved |
| Parameterized type definitions | Supported (substitution at usage site) |
| Explicit / implicit tagging | Parsed + stored |
| AUTOMATIC TAGS | Post-pass assigns sequential tags |
| Tag-based codec dispatch | Not yet generated (codegen uses structural dispatch) |

---

## 14. Known Limitations

**Multiple modules in one file**: fully supported. `AsnParser::parseAll()` reads every `DEFINITIONS … BEGIN … END` block in the file. Each module is resolved with awareness of the others' symbols, and each gets its own nested C++ namespace in the output (`namespace NR_RRC_Definitions { … }`, `namespace NR_UE_Variables { … }`, etc.). Cross-module `IMPORTS` within the same file are resolved correctly; imported value constants (e.g., `maxNrofCellMeas`) are looked up via `resolvedName` when not present in the importing module's local symbol table.

**Multiple-file compilation**: schemas that `IMPORTS` from a *separate* `.asn1` file rely on `FileLoader::findModuleFile` locating that file on disk. If the file cannot be found a warning is emitted but compilation continues. Full separate-file compilation requires providing include paths via the CLI or invoking the compiler with all files pre-loaded.

**Tag-based codec**: generated encode/decode functions use structural
(member-order) dispatch rather than BER/DER/CER tag-based dispatch. This is
correct for UPER (which ignores tags) but means the runtime cannot be used for
BER.

**PER aligned / BER / DER**: only UPER is implemented. The `runtime/per/`
directory is a placeholder.

**Open type encoding**: `openTypeMap` is populated by the resolver but the
codec emitter does not yet emit switch-based dispatch over `openTypeMap` entries.
`ANY DEFINED BY` members encode as raw bytes.

**Fragmented lengths**: `UperLength` handles fragmentation on decode, but the
generated codec loops do not yet call the fragmented form repeatedly for very
large SEQUENCE OF values (>16K elements).

**REAL encoding**: uses IEEE 754 double wrapped as an OCTET STRING, which is the
most common 3GPP practice. Full ASN.1 REAL decimal / special value encoding is
not implemented.

**Error recovery**: the parser throws on the first syntax error. There is no
panic-mode recovery or multi-error accumulation.
