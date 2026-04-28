# ASN.1 Compiler & UPER Codec (C++20 / C99)

A modular ASN.1 compiler that parses ASN.1 schemas and generates either
**C99** or **C++20** structs with UPER (Unaligned Packed Encoding Rules)
encoder/decoder functions. Designed for 3GPP RRC and NGAP protocols. The
compiler itself is written in C++20; the generated output can target either
language.

---

## Quick Start

```bash
# 1 – Configure and build the compiler
mkdir -p build && cd build
cmake -DCMAKE_BUILD_TYPE=Release ..
cmake --build . --parallel

# 2a – Compile an ASN.1 schema → C99 output
./bin/asn1_compiler path/to/schema.asn1 -o generated/my_schema --lang c

# 2b – Compile an ASN.1 schema → C++20 output (default)
./bin/asn1_compiler path/to/schema.asn1 -o generated/my_schema

# 3 – Run all tests
cd build && ctest --output-on-failure
```

### C99 output

`--lang c` produces `my_schema.h` and `my_schema.c`. Include the header and
link against the generated `.c` file compiled as C99 plus the `runtime_c`
static library from `src/runtime/c/`.

### C++20 output (default)

No `--lang` flag needed. Produces `my_schema.h` and `my_schema.cpp`. Each
ASN.1 `DEFINITIONS … BEGIN … END` block becomes a nested C++ namespace
(`asn1::generated::ModuleName`).

---

## Directory Layout

```
asn_compiler/
├── CMakeLists.txt              # Root build — drives everything
├── compile.sh                  # Convenience: cmake configure + build
├── include/
│   ├── frontend/               # Lexer / parser / resolver public headers
│   ├── codegen/
│   │   ├── cpp/                # C++ emitter headers (CppEmitter, CodecEmitter)
│   │   ├── c/                  # C emitter headers   (CEmitter, CCodecEmitter)
│   │   ├── Formatter.h
│   │   ├── TemplateManager.h
│   │   └── TypeMap.h
│   ├── runtime/
│   │   ├── core/               # BitReader, BitWriter, BitUtils (C++)
│   │   ├── uper/               # UPER codec primitives for C++ (Integer, Length, …)
│   │   └── c/                  # C99 runtime headers: asn1_bitwriter.h,
│   │                           #   asn1_bitreader.h, asn1_uper.h
│   └── utils/
├── src/
│   ├── frontend/               # Lexer, parser, resolver, symbol table
│   ├── codegen/
│   │   ├── cpp/                # CppEmitter, CodecEmitter
│   │   ├── c/                  # CEmitter, CCodecEmitter + C runtime source
│   │   ├── Formatter.cpp
│   │   ├── TemplateManager.cpp
│   │   └── TypeMap.cpp
│   ├── runtime/
│   │   ├── core/               # BitReader.cpp, BitWriter.cpp, BitUtils.cpp
│   │   └── uper/               # UperInteger.cpp, UperLength.cpp, …
│   └── utils/                  # FileLoader, Logger, CompilerMain, TestFramework
├── tests/
│   ├── *.cpp                   # 42 unit tests (lexer, parser, resolver, codecs)
│   └── nr-rrc-15.6.0/
│       ├── test_nr_rrc_15_6_0.cpp   # C++ schema integration tests (49 tests)
│       └── test_nr_rrc_15_6_0_c.c  # C99 schema integration tests  (49 tests)
├── schemas/                    # Example ASN.1 input schemas
└── build/                      # CMake output (generated)
    ├── bin/                    # asn1_compiler executable, test binaries
    ├── generated/              # C++  generated .h + .cpp
    ├── generated_c/            # C99  generated .h + .c
    └── dist/                   # Packaged headers + static libs
        ├── include/
        └── lib/
```

---

## CMake Targets

| Target | Type | Purpose |
|---|---|---|
| `frontend` | static lib | Lexer + parser + resolver |
| `runtime_core` | static lib | C++ BitReader / BitWriter / BitUtils |
| `runtime_uper` | static lib | C++ UPER codecs (Integer, Length, Choice, …) |
| `runtime_c` | static lib | C99 runtime (asn1_bitwriter, asn1_bitreader, asn1_uper) |
| `codegen_base` | static lib | TypeMap + Formatter + TemplateManager |
| `codegen_cpp` | static lib | CppEmitter + CodecEmitter (C++ backend) |
| `codegen_c` | static lib | CEmitter + CCodecEmitter (C99 backend) |
| `utils_core` | static lib | FileLoader + Logger |
| `asn1_compiler` | executable | CLI driver |
| `asn1_runtime` | static lib | Combined C++ runtime (runtime_core + runtime_uper) |
| `nr_rrc_15_6_0` | static lib | Generated C++ codec for nr-rrc-15.6.0.asn1 |
| `nr_rrc_15_6_0_c` | static lib | Generated C99 codec for nr-rrc-15.6.0.asn1 |
| `schema_tests_nr_rrc_15_6_0` | executable | C++ schema integration test runner |
| `schema_tests_nr_rrc_15_6_0_c` | executable | C99 schema integration test runner |
| `run_tests` | executable | Unit test runner (42 tests) |

---

## CLI Usage

```
asn1_compiler <input.asn1> -o <output_prefix> [--lang <c|cpp>] [-v]

  -o <prefix>     Output file prefix. Writes <prefix>.h and <prefix>.cpp
                  (C++ mode) or <prefix>.h and <prefix>.c (C mode).
  --lang c        Emit C99 structs + encode_*/decode_* functions.
  --lang cpp      Emit C++20 structs + encode_*/decode_* functions (default).
  -v              Verbose: print the parsed AST before code generation.
```

A single `.asn1` file may contain multiple `DEFINITIONS … BEGIN … END` blocks.
All modules are parsed in one pass.

- **C++** mode: each module becomes a nested `namespace` inside the output files.
- **C** mode: each type and function is prefixed with `ModuleName_` to avoid
  name collisions across modules.

---

## Using Generated C99 Code

### Headers to include

```c
#include "my_schema.h"            // generated types + function prototypes
// (my_schema.h already includes the C runtime headers transitively)
```

### Compile and link

```bash
# Compile your application alongside the generated codec
cc -std=c99 -Iinclude -Ibuild/generated_c \
   my_app.c build/generated_c/my_schema.c \
   -Lbuild/lib -lruntime_c \
   -o my_app
```

Or link against the pre-built static library that CMake produces:

```bash
cc -std=c99 -Iinclude -Ibuild/generated_c \
   my_app.c -Lbuild/lib -lnr_rrc_15_6_0_c -lruntime_c \
   -o my_app
```

### Encode example

```c
#include "nr_rrc_15_6_0.h"
#include "runtime/c/asn1_bitwriter.h"

NR_RRC_Definitions_PhysCellId pci = 42;
char err[256] = {0};

Asn1BitWriter bw;
asn1_bw_init(&bw);

int rc = NR_RRC_Definitions_encode_PhysCellId(&bw, &pci, err, sizeof(err));
if (rc != 0) { fprintf(stderr, "encode error: %s\n", err); }

const uint8_t* buf  = asn1_bw_get_buffer(&bw);
size_t         size = asn1_bw_get_buffer_size(&bw);   /* bytes */

asn1_bw_free(&bw);
```

### Decode example

```c
#include "nr_rrc_15_6_0.h"
#include "runtime/c/asn1_bitreader.h"

extern const uint8_t* wire_bytes;
extern size_t         wire_size;   /* bytes */
char err[256] = {0};

Asn1BitReader br;
asn1_br_init(&br, wire_bytes, wire_size);

NR_RRC_Definitions_PhysCellId pci = 0;
int rc = NR_RRC_Definitions_decode_PhysCellId(&br, &pci, err, sizeof(err));
if (rc != 0) { fprintf(stderr, "decode error: %s\n", err); }
```

### Optional fields

OPTIONAL fields are represented with an `int has_<field>` flag:

```c
NR_RRC_Definitions_RRCSetupRequest_IEs msg = {0};
msg.ue_Identity_tag = NR_RRC_Definitions_RRCSetupRequest_IEs_ue_Identity_randomValue;
msg.ue_Identity_randomValue = 0x1ABCDEULL;
msg.establishmentCause = NR_RRC_Definitions_EstablishmentCause_mo_Data;
/* spare field not present — has_spare stays 0 */
```

### Heap-allocated fields

BIT STRING and OCTET STRING fields own their data on the heap.
Free them before re-use:

```c
NR_RRC_Definitions_CellIdentity ci;
ci.data = malloc(5);
ci.length = 36;   /* bit length */
memcpy(ci.data, my_bytes, 5);

/* ... use ci ... */

free(ci.data);    /* caller is responsible */
```

---

## Using Generated C++20 Code

### Headers to include

```cpp
#include "nr_rrc_15_6_0.h"          // generated types (all three modules)
#include "runtime/core/BitWriter.h"  // if you manipulate bits directly
```

### Encode example

```cpp
#include "nr_rrc_15_6_0.h"

using namespace asn1::generated;

asn1::runtime::BitWriter bw;
NR_RRC_Definitions::RRCSetupRequest_IEs msg{};
msg.ue_Identity = NR_RRC_Definitions::RRCSetupRequest_IEs::ue_Identity_type{
    /* randomValue */ std::uint64_t{0x1ABCDE}
};
msg.establishmentCause = NR_RRC_Definitions::EstablishmentCause::mo_Data;

NR_RRC_Definitions::encode_RRCSetupRequest(bw, msg);
auto bits = bw.data();
```

### Decode example

```cpp
asn1::runtime::BitReader br(bits.data(), bits.size());
auto out = NR_RRC_Definitions::decode_RRCSetupRequest(br);
```

### Module namespaces

All generated types live in `asn1::generated::<ModuleName>`:

```cpp
NR_UE_Variables::VarShortMAC_Input mac{};
NR_UE_Variables::encode_VarShortMAC_Input(bw, mac);

NR_InterNodeDefinitions::MeasurementTimingConfiguration mtc{};
NR_InterNodeDefinitions::encode_MeasurementTimingConfiguration(bw, mtc);
```

---

## Building and Running Tests

### Build + run everything

```bash
# From the repo root
./compile.sh                   # configure + build
cd build && ctest --output-on-failure
```

### Granular build and test commands

```bash
cd build

# Build only the compiler
cmake --build . --target asn1_compiler

# Build + run unit tests (42 tests: lexer, parser, resolver, bit-stream, UPER)
cmake --build . --target run_tests
./bin/run_tests

# Build + run C++ schema integration tests (49 tests)
cmake --build . --target schema_tests_nr_rrc_15_6_0
./bin/schema_tests_nr_rrc_15_6_0

# Build + run C99 schema integration tests (49 tests)
cmake --build . --target schema_tests_nr_rrc_15_6_0_c
./bin/schema_tests_nr_rrc_15_6_0_c

# Run all three test suites via CTest
ctest --output-on-failure

# Run a single named CTest suite
ctest -R SchemaTests_nr_rrc_15_6_0   --output-on-failure
ctest -R SchemaTests_nr_rrc_15_6_0_c --output-on-failure
ctest -R AllTests                     --output-on-failure
```

### Expected output

```
Test project /path/to/build
    Start 1: SchemaTests_nr_rrc_15_6_0
1/3 Test #1: SchemaTests_nr_rrc_15_6_0 ........   Passed    0.02 sec
    Start 2: SchemaTests_nr_rrc_15_6_0_c
2/3 Test #2: SchemaTests_nr_rrc_15_6_0_c ......   Passed    0.00 sec
    Start 3: AllTests
3/3 Test #3: AllTests .........................   Passed    0.00 sec

100% tests passed, 0 tests failed out of 3
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
4. Add a new `CodecEmitter` subclass (or mode flag) that emits calls to PER
   runtime functions instead of UPER ones.

### Adding a New Target Language (e.g., Java)

1. Create `src/codegen/java/` and `include/codegen/java/` mirroring `c/`.
2. Implement `JavaEmitter` (type emission) and `JavaCodecEmitter` (encode/decode
   methods), extending or reusing `TypeMap` and `Formatter`.
3. Add a `CMakeLists.txt` that builds `codegen_java`, link it into `asn1_compiler`.
4. Add a `--lang java` branch in `CompilerMain.cpp` that selects the Java backend.

### Adding a New ASN.1 Feature to the Parser

1. Add any required `TokenType` values to `Token.h` and corresponding keyword
   recognition to `AsnLexer.cpp`.
2. Add any required `NodeType` values to `AsnNode.h`.
3. Implement the grammar production in `AsnParser.cpp`.
4. If the new node type needs symbol resolution, add a case to
   `resolveNodeReferences()` in `SymbolTable.cpp`.
5. Update `CppEmitter.cpp` / `CodecEmitter.cpp` (C++) and/or `CEmitter.cpp` /
   `CCodecEmitter.cpp` (C) as needed.
6. Write a parser test in `tests/test_parser.cpp` and a resolver test in
   `tests/test_resolver.cpp`.

---

## Prerequisites

- CMake ≥ 3.20
- C++20 compiler: GCC 11+, Clang 12+, or MSVC 2019+
- C99 compiler (for building / running C integration tests): any `cc` that supports C99
- No external dependencies (pure standard library)

---

## Future Work

- [ ] PER (aligned) encoding rules in `src/runtime/per/`
- [ ] OER (Octet Encoding Rules) support
- [ ] Java code generation backend
- [ ] Constraint-driven value generation for fuzz testing
- [x] Multiple `DEFINITIONS … BEGIN … END` modules in a single `.asn1` file
- [x] C99 code generation backend (`--lang c`)
- [x] C99 schema integration tests (49 tests, all passing)
- [ ] IMPORTS across multiple schema files at compile time
- [ ] Full 3GPP NR-RRC hex vector regression suite
- [ ] Implement codecs for parameterized-type fields (currently emits TODO stub)
