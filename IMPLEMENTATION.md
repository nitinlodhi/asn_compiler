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
6. [Runtime: Core (BitStream, C++)](#6-runtime-core-bitstream-c)
7. [Runtime: UPER Codecs (C++)](#7-runtime-uper-codecs-c)
8. [Runtime: C99 Runtime](#8-runtime-c99-runtime)
9. [Codegen: TypeMap & Formatter](#9-codegen-typemap--formatter)
10. [Codegen: CppEmitter (C++ types)](#10-codegen-cppemitter-c-types)
11. [Codegen: CodecEmitter (C++ codecs)](#11-codegen-codecemitter-c-codecs)
12. [Codegen: CEmitter (C99 types)](#12-codegen-cemitter-c99-types)
13. [Codegen: CCodecEmitter (C99 codecs)](#13-codegen-ccodechemitter-c99-codecs)
14. [Utils: CLI Driver](#14-utils-cli-driver)
15. [Key Data Structures](#15-key-data-structures)
16. [ASN.1 Features Supported](#16-asn1-features-supported)
17. [Known Limitations](#17-known-limitations)

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
    ├─── --lang cpp (default) ─────────────────────────────────────────────┐
    │  ┌─ for each module ─────────────────────────────────────────────┐   │
    │  │ CppEmitter    src/codegen/cpp/CppEmitter.cpp                  │   │
    │  │   AST → .h types inside namespace                             │   │
    │  │ CodecEmitter  src/codegen/cpp/CodecEmitter.cpp                │   │
    │  │   AST → .cpp  encode_/decode_  functions                      │   │
    │  └────────────────────────────────────────────────────────────────┘   │
    │  Output: <prefix>.h + <prefix>.cpp  (one namespace per module)        │
    └───────────────────────────────────────────────────────────────────────┘
    │
    └─── --lang c ──────────────────────────────────────────────────────────┐
       ┌─ for each module ──────────────────────────────────────────────┐   │
       │ CEmitter      src/codegen/c/CEmitter.cpp                       │   │
       │   AST → .h  typedef struct/enum/union                          │   │
       │ CCodecEmitter src/codegen/c/CCodecEmitter.cpp                  │   │
       │   AST → .c   encode_/decode_  functions                        │   │
       └────────────────────────────────────────────────────────────────┘   │
       Output: <prefix>.h + <prefix>.c  (ModuleName_ prefix per type)       │
       └───────────────────────────────────────────────────────────────────┘
```

The `CompilerMain.cpp` entry point wires these stages together, driven by the
`FileLoader` and `Logger` utilities. The `--lang` flag selects the backend after
the frontend (lexer → parser → resolver) runs identically for both targets.

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
constraints for a type. It is used by the codec emitters to determine encoding
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

## 6. Runtime: Core (BitStream, C++)

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
  ASN.1 BIT STRING members in C++ generated structs.
- `ExtensionValue.h`: `std::any`-based container for open-type values.
- `ObjectIdentifier.h`: thin wrapper around `vector<uint32_t>` for OID arcs.

---

## 7. Runtime: UPER Codecs (C++)

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
`{min, max}` as `optional<long long>` pair. Used by codec emitters at
code-generation time (not at runtime).

---

## 8. Runtime: C99 Runtime

**Files:** `src/runtime/c/asn1_bitwriter.c`, `src/runtime/c/asn1_bitreader.c`,
`src/runtime/c/asn1_uper.c`  
**Headers:** `include/runtime/c/asn1_bitwriter.h`, `include/runtime/c/asn1_bitreader.h`,
`include/runtime/c/asn1_uper.h`

The C99 runtime is a thin C translation of the C++ runtime. Generated C code
includes these headers transitively through the generated `<schema>.h`.

### Asn1BitWriter

```c
typedef struct {
    uint8_t* buffer;   /* heap-allocated, grown on demand */
    size_t   capacity;
    size_t   bit_offset;
} Asn1BitWriter;

void           asn1_bw_init(Asn1BitWriter* bw);        /* alloc + zero */
void           asn1_bw_free(Asn1BitWriter* bw);        /* free buffer  */
void           asn1_bw_write_bits(Asn1BitWriter*, uint64_t value, int n);
void           asn1_bw_write_byte(Asn1BitWriter*, uint8_t value);
void           asn1_bw_write_bytes(Asn1BitWriter*, const uint8_t*, size_t n_bits);
void           asn1_bw_align_to_octet(Asn1BitWriter*);
const uint8_t* asn1_bw_get_buffer(const Asn1BitWriter*);
size_t         asn1_bw_get_buffer_size(const Asn1BitWriter*); /* bytes  */
size_t         asn1_bw_get_bit_offset(const Asn1BitWriter*);  /* bits   */
```

### Asn1BitReader

```c
typedef struct {
    const uint8_t* buffer;
    size_t         buffer_size; /* bytes */
    size_t         bit_offset;
} Asn1BitReader;

void     asn1_br_init(Asn1BitReader*, const uint8_t*, size_t size);
uint64_t asn1_br_read_bits(Asn1BitReader*, int n);
uint8_t  asn1_br_read_byte(Asn1BitReader*);
void     asn1_br_read_bytes(Asn1BitReader*, uint8_t* dest, size_t n_bits);
void     asn1_br_align_to_octet(Asn1BitReader*);
void     asn1_br_skip(Asn1BitReader*, int n);
size_t   asn1_br_get_bit_offset(const Asn1BitReader*);
int      asn1_br_is_at_end(const Asn1BitReader*);
```

### asn1_uper.h

Mirrors the C++ UPER helpers — constrained integer encode/decode, length
determinant, choice index, optional bitmap, extension bit.

---

## 9. Codegen: TypeMap & Formatter

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

## 10. Codegen: CppEmitter (C++ types)

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
- Inline `SEQUENCE_OF` or `SET_OF` whose element is an inline `CHOICE`
  or `SEQUENCE` → emits the element type first, then a `vector<>` alias.

**Second pass**: emits the struct body, mapping each member's effective type
to a C++ field declaration. For optional members, the field is wrapped in
`std::optional<>`. Extension members (after `...`) are wrapped in
`std::optional<>` by convention.

### `emitChoice` — `std::variant`

Iterates the CHOICE alternatives and builds a comma-separated list of C++
type strings for the `std::variant<…>` instantiation. Alternatives that are
themselves inline types are recursively emitted before the variant typedef.

### `emitSequenceOf` — element type resolution

1. If the element node has `resolvedName`, use `TypeMap::resolvedNameToCppRef`.
2. If the element is an inline `CHOICE`, emit a named `_element` alias via
   `emitChoice`, then use that name.
3. If the element is an inline `SEQUENCE`, emit a nested struct via `emitStruct`.
4. Otherwise fall back to `TypeMap::mapAsnToCppType`.

---

## 11. Codegen: CodecEmitter (C++ codecs)

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

**Decoder:**
```cpp
int idx = decodeChoiceIndex(reader, N);
switch (idx) {
  case 0: { Alt0Type v; decode_Alt0(reader, v); return v; } break;
  ...
}
```

### Cycle / recursion detection

Both emitters maintain a `recursion_depth` counter and a `processingNodes`
set keyed on type names. Self-referential types are detected and generate a
forward declaration + pointer-based field rather than infinite recursion.

---

## 12. Codegen: CEmitter (C99 types)

**Files:** `src/codegen/c/CEmitter.cpp`, `include/codegen/c/CEmitter.h`

`CEmitter` produces the `.h` file for the C99 backend. It is structurally
analogous to `CppEmitter` but generates C99 constructs instead of C++20 ones.

### Naming convention

Because C has no namespaces, all generated names include the module prefix:

```
ModuleName_TypeName           typedef struct/enum/union name
ModuleName_TypeName_field_type  inline nested type for struct field
```

`cName(module, name)` produces the prefixed identifier.
`cRef(qualifiedName, currentModule)` converts `"ModuleName.TypeName"` to
`"ModuleName_TypeName"`.

### Type mappings (ASN.1 → C99)

| ASN.1 | C99 |
|---|---|
| INTEGER (constrained) | `uint8_t` / `uint16_t` / `uint32_t` / `int64_t` |
| INTEGER (unconstrained) | `int64_t` |
| BOOLEAN | `int` (0/1) |
| OCTET STRING | `struct { uint8_t* data; size_t length; }` (heap-owned) |
| BIT STRING | `struct { uint8_t* data; size_t length; }` (length = bits) |
| NULL | `int` (placeholder, always 0) |
| REAL | `double` |
| UTF8String / … | `char*` |
| ENUMERATED | `typedef enum { … } TypeName;` |
| SEQUENCE | `typedef struct { … } TypeName;` |
| SEQUENCE OF / SET OF | array struct `{ TypeName* items; size_t count; }` |
| CHOICE | `int tag` + union |

### Optional fields

OPTIONAL fields are represented with an `int has_<field>` sentinel alongside
the value field. There is no `NULL` pointer dereference risk as long as callers
check the flag.

### Parameterized type instantiations

The C backend cannot reference parameterized template types (e.g.,
`SetupRelease { SomeType }`) by name because templates are not emitted — only
concrete instantiations are. When `cTypeFor` encounters a field whose resolved
name points to a parameterized template:

1. It detects that `sym->isParameterized` is true.
2. It calls `emitStruct` / `emitChoice` inline on the resolved type body.
3. It assigns a deterministic name: `ModuleName_ParentTypeName_fieldName_type`.
4. The inline definition is prepended to the parent struct via `pre_emit`.

This means each instantiation site emits its own anonymous named type. The
pattern is invisible to users of the generated header — they see a concrete
struct/union.

---

## 13. Codegen: CCodecEmitter (C99 codecs)

**Files:** `src/codegen/c/CCodecEmitter.cpp`, `include/codegen/c/CCodecEmitter.h`

`CCodecEmitter` produces the `.c` file. For each top-level, non-parameterized
assignment it emits:

```c
int ModuleName_encode_TypeName(Asn1BitWriter* bw, const ModuleName_TypeName* val,
                               char* err_buf, size_t err_buf_size);
int ModuleName_decode_TypeName(Asn1BitReader* br, ModuleName_TypeName* out,
                               char* err_buf, size_t err_buf_size);
```

Return value: `0` on success, non-zero on error (with a message in `err_buf`).

### Dispatch

Same node-type dispatch as `CodecEmitter` but calls `asn1_uper_*` C functions
from `asn1_uper.h` instead of C++ UPER helpers.

### Parameterized-type fields

Fields typed with a parameterized template (e.g., `SetupRelease { X }`) have
their resolved name pointing to a template. The helper `isParameterizedRef`
detects this case and emits a `/* TODO: encode/decode … */` stub instead of
calling a nonexistent codec function. This is safe for schema types that do not
appear in the integration tests; a future improvement is to inline the
substituted codec logic.

```cpp
static bool isParameterizedRef(const frontend::SymbolTable* table,
                                const std::string& resolved_name) {
    auto dot = resolved_name.find('.');
    if (dot == std::string::npos) return false;
    auto sym = table->lookupSymbol(resolved_name.substr(0, dot),
                                   resolved_name.substr(dot + 1));
    return sym && sym->isParameterized;
}
```

---

## 14. Utils: CLI Driver

**Files:** `src/utils/CompilerMain.cpp`, `src/utils/FileLoader.cpp`,
`src/utils/Logger.cpp`

`CompilerMain.cpp::main()`:
1. Parses arguments: `<input.asn1>`, `-o <prefix>`, `--lang <c|cpp>`, `-v`.
2. `FileLoader::loadFile(path)` → `std::string source`.
3. Lex → `AsnLexer(source).tokenize()`.
4. Parse → `AsnParser(tokens).parseAll()` → `vector<AsnNodePtr>`, one element
   per `DEFINITIONS … BEGIN … END` block in the file.
5. For each module AST: register all its ASSIGNMENT symbols in the global
   `SymbolTable`.
6. Call `globalSymbolTable.resolveReferences(all_asts)` — resolves all modules
   in one pass so cross-module references work.

**C++ backend** (default / `--lang cpp`):
7. Emit shared `#ifndef` guard and outer namespace open.
8. For each module AST: topologically sort assignments, skip parameterized
   ones (`node->isParameterized`), open a nested `namespace <ModuleName>`,
   call `CppEmitter` and `CodecEmitter`, close the namespace.
9. Write `<prefix>.h` and `<prefix>.cpp`.

**C backend** (`--lang c`):
7. Emit `#ifndef` guard and C runtime `#include`s.
8. For each module AST: topologically sort assignments, skip parameterized
   ones (`node->isParameterized`), call `CEmitter` (types into `.h`) and
   `CCodecEmitter` (codecs into `.c`).
9. Write `<prefix>.h` (all module types) and `<prefix>.c` (all codec functions).

`Logger` provides `Logger::debug(msg)`, `Logger::info(msg)`, `Logger::error(msg)`
with compile-time level filtering. Verbose mode (`-v`) enables DEBUG level.

---

## 15. Key Data Structures

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

Result of `ConstraintResolver`. Used by codec emitters to select the right
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

## 16. ASN.1 Features Supported

| Feature | C++ | C99 |
|---|---|---|
| SEQUENCE, SET | Supported | Supported |
| SEQUENCE OF, SET OF | Supported | Supported |
| CHOICE | Supported | Supported |
| ENUMERATED | Supported | Supported |
| INTEGER (constrained + unconstrained) | Supported | Supported |
| BOOLEAN | Supported | Supported |
| OCTET STRING | Supported | Supported |
| BIT STRING | Supported | Supported |
| OBJECT IDENTIFIER | Supported | Supported |
| REAL | Supported | Supported |
| NULL | Supported | Supported |
| UTF8String / PrintableString / VisibleString / … | Supported | Supported |
| OPTIONAL members | Supported (`std::optional`) | Supported (`has_` flag) |
| DEFAULT values | Supported (field initialiser) | Supported (zero init) |
| Extension markers (`...`) | Supported | Supported |
| Constraints: range (`0..255`) | Supported | Supported |
| Constraints: SIZE | Supported | Supported |
| Constraints: value reference | Supported | Supported |
| Constraints: WITH COMPONENTS | Supported | Supported |
| Constraints: CONTAINING | Parsed, not used in codegen | Parsed, not used |
| Multiple modules in one file | Supported (namespaces) | Supported (prefixes) |
| IMPORTS within the same file | Supported | Supported |
| IMPORTS across separate files | Partial | Partial |
| Value assignments (`x INTEGER ::= 42`) | Supported | Supported |
| CLASS definitions + object sets | Parsed + resolved | Parsed + resolved |
| Open types (ANY DEFINED BY) | Resolved | Resolved |
| Parameterized type definitions | Instantiated at usage | Instantiated inline |
| Parameterized field codecs | Supported | TODO stub |
| Explicit / implicit tagging | Parsed + stored | Parsed + stored |
| AUTOMATIC TAGS | Post-pass assigns tags | Post-pass assigns tags |

---

## 17. Known Limitations

**Parameterized field codecs (C backend)**: fields typed with a parameterized
template (e.g., `SetupRelease { X }`) emit a `/* TODO */` stub in the C codec.
The C type definition is emitted correctly as an inline instantiation; only the
encode/decode body is missing. None of the current integration tests exercise
these paths so all 49 C tests pass. A correct implementation would inline the
substituted codec logic at the call site.

**Multiple-file compilation**: schemas that `IMPORTS` from a *separate* `.asn1`
file rely on `FileLoader::findModuleFile` locating that file on disk. If the
file cannot be found a warning is emitted but compilation continues. Full
separate-file compilation requires providing include paths via the CLI or
invoking the compiler with all files pre-loaded.

**Tag-based codec**: generated encode/decode functions use structural
(member-order) dispatch rather than BER/DER/CER tag-based dispatch. This is
correct for UPER (which ignores tags) but means the runtime cannot be used for
BER.

**PER aligned / BER / DER**: only UPER is implemented.

**Open type encoding**: `openTypeMap` is populated by the resolver but the
codec emitters do not yet emit switch-based dispatch over `openTypeMap` entries.
`ANY DEFINED BY` members encode as raw bytes.

**Fragmented lengths**: `UperLength` handles fragmentation on decode, but the
generated codec loops do not yet call the fragmented form repeatedly for very
large SEQUENCE OF values (>16K elements).

**REAL encoding**: uses IEEE 754 double wrapped as an OCTET STRING.

**Error recovery**: the parser throws on the first syntax error. There is no
panic-mode recovery or multi-error accumulation.
