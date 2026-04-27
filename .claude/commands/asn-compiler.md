# ASN.1 Compiler — Project Knowledge Base

A slash command knowledge base for working in this repo. Invoke with `/project:asn-compiler` to load context without reading the full codebase.

---

## Project at a glance

| Item | Value |
|---|---|
| Language | C++20, no external deps |
| Build | CMake ≥ 3.20 |
| Encoding | UPER (Unaligned Packed Encoding Rules) |
| Target schemas | 3GPP NR-RRC 15.6.0, 16.1.0 |
| Test count | 42 unit tests + per-schema integration tests |

Quick build:
```bash
cmake -B build && cmake --build build
./build/bin/run_tests          # 42 unit tests
./build/bin/schema_tests_nr_rrc_15_6_0   # integration tests
```

---

## Directory map (key files only)

```
asn_compiler/
├── schemas/                         ASN.1 input schemas
│   ├── nr-rrc-15.6.0.asn1
│   └── nr-rrc-16.1.0.asn1
├── include/
│   ├── frontend/                    Lexer/parser/resolver public headers
│   │   ├── AsnLexer.h / AsnNode.h / AsnParser.h
│   │   ├── SymbolTable.h
│   │   └── ConstraintResolver.h
│   ├── codegen/cpp/
│   │   ├── CppEmitter.h             struct/type emission
│   │   └── CodecEmitter.h           encode_*/decode_* emission
│   └── runtime/
│       ├── core/  BitReader.h  BitWriter.h  BitUtils.h
│       └── uper/  UperInteger.h  UperLength.h  UperChoice.h …
├── src/
│   ├── frontend/
│   │   ├── AsnLexer.cpp
│   │   ├── AsnParser.cpp            parseAll() — multi-module entry point
│   │   ├── SymbolTable.cpp          ALL resolution logic lives here
│   │   └── ConstraintResolver.cpp   resolveBound, constraint extraction
│   ├── codegen/cpp/
│   │   ├── CppEmitter.cpp           emitType, emitSourcePreamble
│   │   └── CodecEmitter.cpp         generateBitStringLogic, generateIntegerLogic …
│   └── utils/CompilerMain.cpp       CLI driver; calls parseAll(), drives codegen
├── tests/
│   ├── test_lexer.cpp / test_parser.cpp / test_resolver.cpp
│   ├── test_bitstream.cpp / test_codecs.cpp
│   └── nr-rrc-15.6.0/
│       └── test_nr_rrc_15_6_0.cpp   schema integration tests (22 tests)
├── CMakeLists.txt                   root build; schema→lib loop; schema test loop
└── build/
    ├── generated/                   .h + .cpp output from asn1_compiler
    └── dist/include/  dist/lib/     staged artefacts for downstream use
```

---

## Compiler pipeline

```
.asn1 file
  └─► AsnLexer       tokenises keywords, identifiers, numbers, strings
        └─► AsnParser.parseAll()   returns vector<AsnNodePtr>, one per module
              └─► SymbolTable      register all module symbols
                    └─► resolveNodeReferences()   recursive post-order resolution
                          └─► ConstraintResolver  extracts SIZE / range bounds
                                └─► CppEmitter    emits struct/variant/enum/alias
                                      └─► CodecEmitter   emits encode_*/decode_*
```

### Key invariants

- **`parseAll()`** loops until `END_OF_FILE`; each call to `parseModuleDefinition()` consumes one `DEFINITIONS … BEGIN … END` block.
- **`resolvedName`** on `AsnNode` stores the qualified `"ModuleName.SymbolName"` form set by the resolver. Always check `node->resolvedName.has_value()` before re-resolving; early-exit guard prevents wrong-module re-resolution.
- **IMPORTS children** are plain name strings, not type expressions — the resolver skips recursion into `NodeType::IMPORTS` to avoid mistaking them for unresolved types.
- **Topological sort** of assignments within each module ensures C++ forward references are avoided. Cross-module refs resolve because all modules are registered first.
- **`ConstraintResolver::resolveBound`** falls back to `resolvedName` for cross-module value constants (e.g., `maxNrofCellMeas` defined in a different module).

---

## Generated code structure

For a schema with modules `M1`, `M2`, `M3`, output is a single `.h`/`.cpp` pair:

```cpp
// <prefix>.h
namespace asn1::generated {

namespace M1 {
  // types and encode_*/decode_* declarations
} // namespace M1

namespace M2 {
  // can reference M1:: types directly since same header
} // namespace M2

} // namespace asn1::generated
```

Types from other modules are referenced as `NR_RRC_Definitions::PhysCellId`, etc.

---

## Adding a new ASN.1 feature (checklist)

1. **Lexer** (`AsnLexer.cpp`): add `TokenType` + keyword string mapping.
2. **Parser** (`AsnParser.cpp`): add `NodeType` + `parseXxx()` method; call from the right production.
3. **Resolver** (`SymbolTable.cpp`): add case in `resolveNodeReferences()`. For object-class features, follow the FIELD_REFERENCE pattern: check if child(0) is `CLASS_DEFINITION` or `IDENTIFIER` (object set).
4. **Constraint** (`ConstraintResolver.cpp`): update `extractConstraint()` if new node has bounds.
5. **Struct emitter** (`CppEmitter.cpp`): add case in `emitType()`.
6. **Codec emitter** (`CodecEmitter.cpp`): add `generateXxxLogic()` + call from `generateEncodeBody()` / `generateDecodeBody()`.
7. **Tests**: `tests/test_parser.cpp` (parser test), `tests/test_resolver.cpp` (resolver test).

---

## Adding constraint validation (pattern)

Every `generateXxxLogic()` in `CodecEmitter.cpp` follows this pattern for **encoder**:

```cpp
// 1. validate before encoding
code += formatter.formatCode("if (value < min || value > max) {\n");
formatter.indent();
code += formatter.formatCode("throw std::runtime_error(\"INTEGER constraint violation: ...\");\n");
formatter.dedent();
code += formatter.formatCode("}\n");
// 2. encode
code += formatter.formatCode("UperInteger::encodeConstrainedInt(writer, value, min, max);\n");
```

And for **decoder** — validate AFTER decoding:

```cpp
auto val = UperInteger::decodeConstrainedInt(reader, min, max);
if (val < min || val > max) throw std::runtime_error(...);
```

For BIT STRING SIZE(N): check `value.bit_length == N` before `UperBitString::encode(writer, value, N)`.

Exception type is always `std::runtime_error`. The preamble (`emitSourcePreamble`) adds `#include <stdexcept>` and `#include <string>`.

---

## Schema integration test pattern

Each schema `schemas/foo-bar.asn1` → test at `tests/foo-bar/test_foo_bar.cpp`.

The root `CMakeLists.txt` automatically discovers and wires these into CTest using:

```cmake
foreach(SCHEMA ${ASN1_SCHEMAS})
    # derives _SCHEMA_STEM and LIB_NAME from schema path
    set(_SCHEMA_TEST_SRC tests/${_SCHEMA_STEM}/test_${LIB_NAME}.cpp)
    if(EXISTS ${_SCHEMA_TEST_SRC})
        add_executable(schema_tests_${LIB_NAME} ${_SCHEMA_TEST_SRC})
        target_link_libraries(schema_tests_${LIB_NAME} PRIVATE ${LIB_NAME})
        add_test(NAME SchemaTests_${LIB_NAME} COMMAND schema_tests_${LIB_NAME})
    endif()
endforeach()
```

**To add a new schema test:**
1. Add the `.asn1` to `ASN1_SCHEMAS` in root `CMakeLists.txt`.
2. Create `tests/<schema-stem>/test_<lib_name>.cpp` using the same helper pattern (`expect_ok`, `expect_throw`).
3. Run `cmake .. && cmake --build . --target schema_tests_<lib_name>`.

Test helper pattern (copy-paste friendly):
```cpp
template <typename F>
static void expect_ok(const std::string& name, F&& f) {
    try { f(); std::cout << "[PASS] " << name << "\n"; ++passed; }
    catch (const std::exception& e) { std::cout << "[FAIL] " << name << ": " << e.what() << "\n"; ++failed; }
}
template <typename F>
static void expect_throw(const std::string& name, const std::string& substr, F&& f) {
    try { f(); std::cout << "[FAIL] " << name << ": expected exception\n"; ++failed; }
    catch (const std::runtime_error& e) {
        if (std::string(e.what()).find(substr) != std::string::npos)
            std::cout << "[PASS] " << name << "\n", ++passed;
        else
            std::cout << "[FAIL] " << name << ": msg=" << e.what() << "\n", ++failed;
    }
}
```

---

## Common gotchas

| Symptom | Root cause | Fix |
|---|---|---|
| "Symbol 'X' is not an information object class" | FIELD_REFERENCE LHS is an object set (IDENTIFIER child), not a CLASS_DEFINITION | Check `child(0)->type`; if IDENTIFIER, look up the class it names |
| "Undefined type 'TYPE'" | `TYPE` keyword in open-type field treated as a type name | Add to `fieldPrimitives` static set in `SymbolTable.cpp` |
| "Mismatched number of parameters for type 'SetupRelease'" | Resolver recurses into IMPORTS children (which are names, not types) | Guard: `if (node->type == NodeType::IMPORTS) continue;` |
| "Undefined value reference 'maxXxx' in constraint" | Cross-module value constant; `resolvedName` set but `resolveBound` only checks current module | Add fallback: if symbol not found in current module, split `resolvedName` and look up in defining module |
| Re-resolution with wrong module scope | `resolveNodeReferences` called again after first pass sets `resolvedName` | Early-exit guard: `if (node->resolvedName.has_value()) return;` at top of IDENTIFIER case |
| Constraint violation not thrown at runtime | Generator missing validation check | Add pre-encode range/size check in `generateXxxLogic()` using `std::runtime_error` |

---

## Runtime library API (for generated-code consumers)

```cpp
// BitWriter
asn1::runtime::BitWriter bw;
bw.writeBits(value, numBits);
const uint8_t* buf = bw.getBuffer();
size_t         sz  = bw.getBufferSize();
size_t         off = bw.getBitOffset();  // bits written % 8

// BitReader
asn1::runtime::BitReader br(buf, sz);
uint64_t v = br.readBits(numBits);
```

UPER primitives live in `include/runtime/uper/`:
- `UperInteger::encodeConstrainedInt(writer, value, min, max)`
- `UperInteger::decodeConstrainedInt(reader, min, max)`
- `UperLength::encodeLength(writer, len, min, max)`
- `UperLength::decodeLength(reader, min, max)`
- `UperBitString::encode(writer, bs, fixedSize)` / `::decode(reader, fixedSize)`
- `UperChoice::encode(writer, idx, numAlts)` / `::decode(reader, numAlts)`

---

## Known limitations (as of 2026-04-26)

- No PER (aligned) or OER support.
- No `IMPORTS` resolution across *separate* `.asn1` files at compile time (multi-module within one file works).
- Extension markers (`...`) parsed but not fully honoured in codec (extension additions ignored).
- `CONTAINING` / `ENCODED BY` constraints not implemented.
- No Java/Python code-gen backend.
