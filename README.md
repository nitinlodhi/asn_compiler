# ASN.1 Compiler & UPER Codec (C++20)

A modular ASN.1 compiler that parses ASN.1 schemas and generates C++ structs with
UPER (Unaligned Packed Encoding Rules) encoder/decoder functions. Designed for
3GPP RRC and NGAP protocols, and structured to support additional encoding rules
and target languages in the future.

---

## Quick Start

```bash
# Configure and build
mkdir -p build && cd build
cmake -DCMAKE_BUILD_TYPE=Release ..
cmake --build .

# Compile an ASN.1 schema to C++
./bin/asn1_compiler path/to/schema.asn1 -o output_prefix

# Run the test suite
./bin/run_tests
```

This produces `output_prefix.h` and `output_prefix.cpp` which you compile
alongside the runtime headers from `include/runtime/`.

A single `.asn1` file may contain multiple `DEFINITIONS … BEGIN … END` blocks.
All modules are parsed in one pass and each gets its own nested C++ namespace
inside the output files (e.g., `asn1::generated::NR_RRC_Definitions`,
`asn1::generated::NR_UE_Variables`).

---

## Directory Layout

```
asn_compiler/
├── CMakeLists.txt              # Root build — drives everything
├── include/
│   ├── frontend/               # Lexer / parser / resolver public headers
│   ├── codegen/
│   │   ├── cpp/                # C++ emitter headers
│   │   ├── Formatter.h
│   │   ├── TemplateManager.h
│   │   └── TypeMap.h
│   ├── runtime/
│   │   ├── core/               # BitReader, BitWriter, BitUtils, helpers
│   │   └── uper/               # UPER codec primitives (Integer, Length, …)
│   └── utils/
├── src/
│   ├── frontend/               # Lexer, parser, resolver, symbol table
│   ├── codegen/
│   │   ├── cpp/                # CppEmitter, CodecEmitter
│   │   ├── Formatter.cpp
│   │   ├── TemplateManager.cpp
│   │   └── TypeMap.cpp
│   ├── runtime/
│   │   ├── core/               # BitReader.cpp, BitWriter.cpp, BitUtils.cpp
│   │   └── uper/               # UperInteger.cpp, UperLength.cpp, …
│   └── utils/                  # FileLoader, Logger, CompilerMain, TestFramework
├── tests/                      # Unit tests (42 tests, all green)
├── schemas/                    # Example ASN.1 input schemas
└── build/                      # CMake output (generated)
```

---

## CMake Targets

| Target | Type | Purpose |
|---|---|---|
| `frontend` | static lib | Lexer + parser + resolver |
| `runtime_core` | static lib | BitReader / BitWriter / BitUtils |
| `runtime_uper` | static lib | UPER codecs (Integer, Length, Choice, …) |
| `codegen_base` | static lib | TypeMap + Formatter + TemplateManager |
| `codegen_cpp` | static lib | CppEmitter + CodecEmitter |
| `utils_core` | static lib | FileLoader + Logger |
| `asn1_compiler` | executable | CLI driver |
| `asn1_runtime` | interface lib | Convenience alias: `runtime_core + runtime_uper` |
| `run_tests` | executable | Full test suite |

The root `CMakeLists.txt` also defines per-schema build rules: for every `.asn1`
file listed in `ASN1_SCHEMAS`, CMake will run `asn1_compiler` to generate a
`.h`/`.cpp` pair, compile it into a static library, and stage the artefacts under
`build/dist/`.

---

## CLI Usage

```
asn1_compiler <input.asn1> -o <output_prefix> [-v]

  -o <prefix>   Output file prefix. Writes <prefix>.h and <prefix>.cpp.
  -v            Verbose: print the parsed AST before code generation.
```

---

## Using Generated Code

The generated files depend only on the runtime headers. Copy them into your
project alongside `include/runtime/` and link against the static libraries built
from `src/runtime/`:

```cpp
#include "nr_rrc_15_6_0.h"          // generated types (all three modules)
#include "runtime/core/BitWriter.h"  // if you manipulate bits directly

using namespace asn1::generated;

// Encode (types are in their module namespace)
asn1::runtime::BitWriter bw;
NR_RRC_Definitions::encode_RRCSetupRequest(bw, rrcMsg);
auto bits = bw.data();

// Decode
asn1::runtime::BitReader br(bits.data(), bits.size());
auto out = NR_RRC_Definitions::decode_RRCSetupRequest(br);

// UE-Variables types live in their own namespace
NR_UE_Variables::VarShortMAC_Input mac{};
NR_UE_Variables::encode_VarShortMAC_Input(bw, mac);
```

---

## Extending the Compiler

### Adding Support for a New Encoding Rule (e.g., PER, BER)

1. Create `src/runtime/per/` and `include/runtime/per/` mirroring the `uper/`
   layout.
2. Implement codec primitives (`PerInteger.cpp`, `PerLength.cpp`, …) using the
   same `BitReader`/`BitWriter` from `runtime_core`.
3. Add a `CMakeLists.txt` in `src/runtime/per/` building a `runtime_per` static
   library, and register it with `add_subdirectory` in `src/runtime/CMakeLists.txt`.
4. Add a new `CodecEmitter` subclass (or mode flag) in `src/codegen/` that emits
   calls to PER runtime functions instead of UPER ones.

### Adding a New Target Language (e.g., Java)

1. Create `src/codegen/java/` and `include/codegen/java/` mirroring `cpp/`.
2. Implement `JavaEmitter` (struct emission) and `JavaCodecEmitter` (encode/decode
   methods), extending or reusing `TypeMap` and `Formatter`.
3. Add a `CMakeLists.txt` that builds `codegen_java`, link it into `asn1_compiler`.
4. Add a `-lang java` CLI flag in `CompilerMain.cpp` that selects the Java backend.

### Adding a New ASN.1 Feature to the Parser

1. Add any required `TokenType` values to `Token.h` and corresponding keyword
   recognition to `AsnLexer.cpp`.
2. Add any required `NodeType` values to `AsnNode.h`.
3. Implement the grammar production in `AsnParser.cpp`. Parser methods follow
   the pattern `parseXxx()` returning `AsnNodePtr`.
4. If the new node type needs symbol resolution, add a case to
   `resolveNodeReferences()` in `SymbolTable.cpp`.
5. If it affects generated C++ types, update `CppEmitter.cpp`; if it affects
   codec logic, update `CodecEmitter.cpp`.
6. Write a parser test in `tests/test_parser.cpp` and a resolver test in
   `tests/test_resolver.cpp`.

---

## Running Tests

```bash
# Build and run
cmake --build build --target run_tests
./build/bin/run_tests

# Or via CTest
cd build && ctest --output-on-failure
```

All 42 tests cover: lexer tokenisation, parser grammar, resolver (symbol table,
constraints, open types, WITH COMPONENTS, automatic tagging), BitStream I/O,
and UPER codec primitives.

---

## Prerequisites

- CMake ≥ 3.20
- C++20 compiler: GCC 11+, Clang 12+, or MSVC 2019+
- No external dependencies (pure C++20 standard library)

---

## Future Work

- [ ] PER (aligned) encoding rules in `src/runtime/per/`
- [ ] OER (Octet Encoding Rules) support
- [ ] Java code generation backend
- [ ] Constraint-driven value generation for fuzz testing
- [x] Multiple `DEFINITIONS … BEGIN … END` modules in a single `.asn1` file
- [ ] ASN.1 `IMPORTS` across multiple schema files at compile time
- [ ] Full 3GPP NR-RRC hex vector regression suite
