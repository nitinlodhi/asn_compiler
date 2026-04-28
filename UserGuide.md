# ASN.1 Compiler — User Guide

This guide explains how to build the compiler, compile your own ASN.1 schemas
to C99 or C++20, integrate the generated code into your application, and
validate everything with the test suite.

---

## Table of Contents

1. [Prerequisites](#1-prerequisites)
2. [Building the Compiler](#2-building-the-compiler)
3. [Compiling an ASN.1 Schema](#3-compiling-an-asn1-schema)
   - [C99 output (`--lang c`)](#c99-output---lang-c)
   - [C++20 output (default)](#c20-output-default)
4. [Using Generated C99 Code](#4-using-generated-c99-code)
5. [Using Generated C++20 Code](#5-using-generated-c20-code)
6. [Build and Validation Commands](#6-build-and-validation-commands)
   - [Full build](#full-build)
   - [Incremental builds](#incremental-builds)
   - [Running tests](#running-tests)
   - [Validating C99 tests](#validating-c99-tests)
   - [Validating C++ tests](#validating-c-tests)
7. [Adding a New ASN.1 Schema](#7-adding-a-new-asn1-schema)
8. [Dist Package Layout](#8-dist-package-layout)
9. [Troubleshooting](#9-troubleshooting)

---

## 1. Prerequisites

| Tool | Minimum version | Notes |
|---|---|---|
| CMake | 3.20 | Build system |
| C++ compiler | GCC 11 / Clang 12 / MSVC 2019 | Must support C++20 |
| C compiler | Any C99-capable `cc` | For building C99 tests and C runtime |

No external libraries are required. The compiler depends only on the C++20
standard library. Generated C code depends only on the bundled `runtime_c`
library.

---

## 2. Building the Compiler

```bash
# Clone or enter the project root
cd asn_compiler

# Configure (Release is recommended; use Debug for stepping through the compiler)
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release

# Build everything in parallel
cmake --build build --parallel

# Alternatively, the convenience script does both steps:
./compile.sh
```

After a successful build the following key artefacts exist:

```
build/
├── bin/
│   ├── asn1_compiler              # The compiler executable
│   ├── run_tests                  # Unit test runner
│   ├── schema_tests_nr_rrc_15_6_0   # C++ schema integration tests
│   └── schema_tests_nr_rrc_15_6_0_c # C99  schema integration tests
├── generated/                     # C++  output for bundled schemas
│   ├── nr_rrc_15_6_0.h
│   └── nr_rrc_15_6_0.cpp
├── generated_c/                   # C99  output for bundled schemas
│   ├── nr_rrc_15_6_0.h
│   └── nr_rrc_15_6_0.c
├── lib/
│   ├── libnr_rrc_15_6_0.a         # C++ codec static library
│   ├── libnr_rrc_15_6_0_c.a       # C99  codec static library
│   ├── libasn1_runtime.a          # C++ runtime
│   └── libruntime_c.a             # C99  runtime
└── dist/
    ├── include/                   # Packaged headers (copy to your project)
    └── lib/                       # Packaged static libs
```

---

## 3. Compiling an ASN.1 Schema

### C99 output (`--lang c`)

```bash
./build/bin/asn1_compiler my_schema.asn1 -o out/my_schema --lang c
```

Produces:

- `out/my_schema.h` — `typedef struct`, `typedef enum`, function prototypes.
  Includes the C runtime headers automatically.
- `out/my_schema.c` — encode/decode function bodies. Must be compiled as C99.

### C++20 output (default)

```bash
./build/bin/asn1_compiler my_schema.asn1 -o out/my_schema
# or explicitly:
./build/bin/asn1_compiler my_schema.asn1 -o out/my_schema --lang cpp
```

Produces:

- `out/my_schema.h` — `struct`, `enum class`, `std::variant`, `std::vector`
  types wrapped in namespaces.
- `out/my_schema.cpp` — encode/decode function bodies.

### Verbose output (dump the AST)

```bash
./build/bin/asn1_compiler my_schema.asn1 -o out/my_schema --lang c -v
```

The `-v` flag prints the parsed AST to stdout before code generation.
Useful for diagnosing parser or resolver issues.

### Multi-module schemas

If your `.asn1` file contains multiple `DEFINITIONS … BEGIN … END` blocks
(e.g., 3GPP NR-RRC), a **single compiler invocation** handles all of them:

```bash
./build/bin/asn1_compiler schemas/nr-rrc-15.6.0.asn1 -o out/nr_rrc --lang c
```

All modules are parsed together so cross-module `IMPORTS` and type references
resolve correctly. Each module contributes its types to the same output files.

- **C99**: types and functions for every module land in the same `.h`/`.c`,
  each prefixed with the module name (`NR_RRC_Definitions_`, `NR_UE_Variables_`,
  `NR_InterNodeDefinitions_`).
- **C++**: each module is a nested namespace
  (`asn1::generated::NR_RRC_Definitions`, etc.) inside the same `.h`/`.cpp`.

---

## 4. Using Generated C99 Code

### Project layout

```
your_project/
├── include/
│   └── runtime/c/        ← copy from asn_compiler/include/runtime/c/
├── out/
│   ├── my_schema.h
│   └── my_schema.c
└── main.c
```

### Compile and link (manual)

```bash
cc -std=c99 \
   -Iinclude \
   -Iout \
   main.c out/my_schema.c path/to/libruntime_c.a \
   -o my_app
```

Or link against the pre-built static library from `build/lib/`:

```bash
cc -std=c99 \
   -Iasn_compiler/include \
   -Iasn_compiler/build/generated_c \
   main.c \
   -Lasn_compiler/build/lib -lnr_rrc_15_6_0_c -lruntime_c \
   -o my_app
```

### API conventions for generated C code

**Function signatures**

```c
// Encode: returns 0 on success, non-zero on error
int ModuleName_encode_TypeName(
    Asn1BitWriter*        bw,        // initialised writer
    const ModuleName_TypeName* val,  // value to encode
    char*                 err_buf,   // error message output (may be NULL)
    size_t                err_buf_size
);

// Decode: returns 0 on success, non-zero on error
int ModuleName_decode_TypeName(
    Asn1BitReader*    br,       // reader positioned at start of encoding
    ModuleName_TypeName* out,   // populated on success
    char*             err_buf,
    size_t            err_buf_size
);
```

**Encode example**

```c
#include "nr_rrc_15_6_0.h"

void encode_example(void) {
    NR_RRC_Definitions_PhysCellId pci = 42;
    char err[256] = {0};

    Asn1BitWriter bw;
    asn1_bw_init(&bw);

    int rc = NR_RRC_Definitions_encode_PhysCellId(&bw, &pci, err, sizeof(err));
    if (rc != 0) {
        fprintf(stderr, "encode failed: %s\n", err);
        asn1_bw_free(&bw);
        return;
    }

    const uint8_t* buf  = asn1_bw_get_buffer(&bw);      /* wire bytes   */
    size_t         size = asn1_bw_get_buffer_size(&bw);  /* byte count   */

    /* use buf / size ... */

    asn1_bw_free(&bw);   /* always free, even on error */
}
```

**Decode example**

```c
#include "nr_rrc_15_6_0.h"

void decode_example(const uint8_t* wire, size_t wire_len) {
    char err[256] = {0};

    Asn1BitReader br;
    asn1_br_init(&br, wire, wire_len);

    NR_RRC_Definitions_PhysCellId pci = 0;
    int rc = NR_RRC_Definitions_decode_PhysCellId(&br, &pci, err, sizeof(err));
    if (rc != 0) {
        fprintf(stderr, "decode failed: %s\n", err);
        return;
    }
    printf("PhysCellId = %u\n", (unsigned)pci);
}
```

**CHOICE types**

CHOICE fields are represented as a `tag` integer plus a union:

```c
NR_RRC_Definitions_RRCSetupRequest_IEs msg;
memset(&msg, 0, sizeof(msg));

/* Select the randomValue alternative */
msg.ue_Identity_tag = NR_RRC_Definitions_RRCSetupRequest_IEs_ue_Identity_randomValue;
msg.ue_Identity_randomValue = 0x1ABCDEull;
msg.establishmentCause = NR_RRC_Definitions_EstablishmentCause_mo_Data;
```

**OPTIONAL fields**

```c
NR_RRC_Definitions_SomeSequence s;
memset(&s, 0, sizeof(s));

/* Field present */
s.has_optionalField = 1;
s.optionalField = 99;

/* Field absent — has_ stays 0 */
```

**Heap-allocated fields (BIT STRING, OCTET STRING)**

```c
NR_RRC_Definitions_CellIdentity ci;
ci.data   = malloc(5);          /* ceil(36 / 8) = 5 bytes */
ci.length = 36;                 /* bit count */
memcpy(ci.data, source_bytes, 5);

/* ... use ci ... */

free(ci.data);                  /* caller must free */
```

**SEQUENCE OF fields**

```c
NR_RRC_Definitions_DRB_CountMSB_InfoList list;
list.count = 2;
list.items = calloc(list.count, sizeof(*list.items));

/* populate list.items[0], list.items[1] ... */

free(list.items);
```

---

## 5. Using Generated C++20 Code

### Project layout

```
your_project/
├── include/
│   └── runtime/          ← copy from asn_compiler/include/runtime/
├── out/
│   ├── my_schema.h
│   └── my_schema.cpp
└── main.cpp
```

### Compile and link (manual)

```bash
c++ -std=c++20 \
    -Iinclude \
    -Iout \
    main.cpp out/my_schema.cpp path/to/libasn1_runtime.a \
    -o my_app
```

### API conventions for generated C++ code

**Module namespaces**

Every module lives in `asn1::generated::<ModuleName>`:

```cpp
#include "nr_rrc_15_6_0.h"
using namespace asn1::generated;

NR_RRC_Definitions::PhysCellId pci = 42;
NR_UE_Variables::VarShortMAC_Input mac{};
NR_InterNodeDefinitions::MeasurementTimingConfiguration mtc{};
```

**Encode**

```cpp
asn1::runtime::BitWriter bw;
NR_RRC_Definitions::encode_PhysCellId(bw, pci);
std::vector<uint8_t> wire = bw.data();
```

**Decode**

```cpp
asn1::runtime::BitReader br(wire.data(), wire.size());
auto pci = NR_RRC_Definitions::decode_PhysCellId(br);
```

**OPTIONAL fields**

Optional fields are `std::optional<T>`:

```cpp
NR_RRC_Definitions::SomeSequence s;
s.optionalField = 42;               // present
s.anotherOptional = std::nullopt;   // absent (default)
```

**CHOICE fields**

CHOICE alternatives are `std::variant<Alt0, Alt1, …>`:

```cpp
NR_RRC_Definitions::RRCSetupRequest_IEs::ue_Identity_type id =
    uint64_t{0x1ABCDE};   /* randomValue alternative */
```

---

## 6. Build and Validation Commands

### Full build

```bash
# From the repo root — configure + build everything
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel

# Shortcut
./compile.sh
```

### Incremental builds

After changing source files CMake rebuilds only what changed:

```bash
cmake --build build --parallel
```

To rebuild just the compiler binary:

```bash
cmake --build build --target asn1_compiler
```

To regenerate C output for a specific schema and rebuild its library:

```bash
cmake --build build --target nr_rrc_15_6_0_c
```

To regenerate C++ output:

```bash
cmake --build build --target nr_rrc_15_6_0
```

### Running all tests

```bash
cd build
ctest --output-on-failure
```

Expected output:

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

### Validating C99 tests

```bash
# Build the C99 schema integration test binary
cmake --build build --target schema_tests_nr_rrc_15_6_0_c

# Run it directly (shows per-test PASS/FAIL lines)
./build/bin/schema_tests_nr_rrc_15_6_0_c

# Run it through CTest (shows pass/fail summary only)
cd build && ctest -R SchemaTests_nr_rrc_15_6_0_c --output-on-failure
```

The C99 test binary runs 49 encode/decode round-trip tests covering all
major NR-RRC types. Each test prints `PASS` or `FAIL <reason>` and exits
with code 0 if all pass, non-zero otherwise.

### Validating C++ tests

```bash
# Build the C++ schema integration test binary
cmake --build build --target schema_tests_nr_rrc_15_6_0

# Run it directly
./build/bin/schema_tests_nr_rrc_15_6_0

# Run it through CTest
cd build && ctest -R SchemaTests_nr_rrc_15_6_0 --output-on-failure
```

### Validating unit tests

```bash
# Build the unit test binary (42 tests: lexer, parser, resolver, bit-stream, UPER)
cmake --build build --target run_tests

# Run directly
./build/bin/run_tests

# Run via CTest
cd build && ctest -R AllTests --output-on-failure
```

### Validate everything in one command

```bash
cmake --build build --parallel && cd build && ctest --output-on-failure
```

---

## 7. Adding a New ASN.1 Schema

### Step 1 — Place the schema file

```bash
cp your_schema.asn1 asn_compiler/schemas/
```

### Step 2 — Register it in CMakeLists.txt

Open `CMakeLists.txt` and add your file to the `ASN1_SCHEMAS` list:

```cmake
set(ASN1_SCHEMAS
    schemas/nr-rrc-15.6.0.asn1
    schemas/your_schema.asn1    # ← add this line
)
```

CMake will automatically:

- Derive `your_schema` → `your_schema` (or mangled, e.g. `your_schema_1_0`)
  as the library name.
- Add a `add_custom_command` that runs `asn1_compiler` to generate
  `build/generated_c/your_schema.h` + `.c` (C99) and
  `build/generated/your_schema.h` + `.cpp` (C++).
- Build static libraries `your_schema` (C++) and `your_schema_c` (C99).
- Stage the headers and libs into `build/dist/`.

### Step 3 — Rebuild

```bash
cmake --build build --parallel
```

### Step 4 — Add integration tests (optional but recommended)

Create a test directory matching the schema stem:

```
tests/your-schema/
├── test_your_schema.cpp    # C++ tests
└── test_your_schema_c.c    # C99 tests
```

CMake automatically detects and builds these when the files exist. The test
source must define a `main()` that returns 0 on all-pass and non-zero on any
failure.

Minimal C99 test template:

```c
#include <stdio.h>
#include "your_schema.h"

static int failures = 0;
#define CHECK(cond, msg) do { \
    if (!(cond)) { fprintf(stderr, "FAIL: %s\n", msg); failures++; } \
    else { printf("PASS: %s\n", msg); } \
} while(0)

int main(void) {
    char err[256];
    Asn1BitWriter bw;
    asn1_bw_init(&bw);

    /* --- encode --- */
    YourModule_SomeType val = 7;
    int rc = YourModule_encode_SomeType(&bw, &val, err, sizeof(err));
    CHECK(rc == 0, "encode SomeType");

    /* --- decode --- */
    Asn1BitReader br;
    asn1_br_init(&br, asn1_bw_get_buffer(&bw), asn1_bw_get_buffer_size(&bw));
    YourModule_SomeType out = 0;
    rc = YourModule_decode_SomeType(&br, &out, err, sizeof(err));
    CHECK(rc == 0, "decode SomeType");
    CHECK(out == 7, "round-trip value");

    asn1_bw_free(&bw);
    return failures > 0 ? 1 : 0;
}
```

Minimal C++ test template:

```cpp
#include <cstdio>
#include "your_schema.h"
#include "runtime/core/BitWriter.h"
#include "runtime/core/BitReader.h"

static int failures = 0;
#define CHECK(cond, msg) do { \
    if (!(cond)) { std::fprintf(stderr, "FAIL: %s\n", msg); failures++; } \
    else { std::printf("PASS: %s\n", msg); } \
} while(0)

int main() {
    using namespace asn1::generated;
    asn1::runtime::BitWriter bw;

    YourModule::SomeType val = 7;
    YourModule::encode_SomeType(bw, val);

    auto buf = bw.data();
    asn1::runtime::BitReader br(buf.data(), buf.size());
    auto out = YourModule::decode_SomeType(br);

    CHECK(out == 7, "round-trip SomeType");
    return failures > 0 ? 1 : 0;
}
```

After adding the test files, rebuild and run:

```bash
cmake --build build --parallel
cd build && ctest --output-on-failure
```

---

## 8. Dist Package Layout

After a full build, `build/dist/` contains a self-contained package suitable
for embedding into another CMake project or Makefile:

```
build/dist/
├── include/
│   ├── nr_rrc_15_6_0.h          # C++ generated header
│   └── runtime/
│       ├── core/                # BitWriter.h, BitReader.h, BitUtils.h, …
│       └── uper/                # UperInteger.h, UperLength.h, …
└── lib/
    ├── libnr_rrc_15_6_0.a       # C++ codec library
    ├── libasn1_runtime.a        # C++ runtime (core + uper)
    └── (C99 libs are in build/lib/ — not yet staged to dist/)
```

> The C99 generated header lives at `build/generated_c/nr_rrc_15_6_0.h` and
> the C runtime headers at `include/runtime/c/`. They are not yet automatically
> staged into `dist/`; copy them manually as needed.

---

## 9. Troubleshooting

### Compiler binary not found

```
Error: cannot find asn1_compiler
```

Make sure the build succeeded:

```bash
cmake --build build --target asn1_compiler && ls build/bin/asn1_compiler
```

### Generated header includes undefined type

If the generated header references a type like `SomeModule_SomeParameterizedType`
that does not exist, the schema uses a parameterized type template (e.g.,
`SetupRelease { ElementType }`). The C backend does not emit template
definitions. Each usage site should instead emit an inline instantiation. If you
see this, it likely means a new parameterized usage site was added to the schema
that the emitter has not handled yet. Run with `-v` to inspect the AST and file
an issue.

### `undefined reference to encode_Xyz` / `decode_Xyz`

You are linking against the generated `.a` but the codec for that type was not
emitted (parameterized type or TODO stub). Check the generated `.c` file for
`/* TODO */` comments. If the codec body is missing, the type is on the
known-limitation list for the C backend.

### Parse error on a valid schema

The parser is strict and stops at the first syntax error. Run with `-v` to see
how far parsing got. Common causes:
- Comment style: use `--` comments only (not `/* */` inside ASN.1 type bodies).
- Bit string literals: `'0110'B` must use single quotes.
- Missing `END` keyword at the end of a module.

### Cross-module import not resolving

Check that all modules are in the **same `.asn1` file** or that the imported
file is in the same directory. The compiler searches for imported module files
relative to the input file's directory. Multi-file support is partial; 3GPP
NR-RRC works because all three modules are in one file.

### C test fails to compile (strict C99)

The generated C code and test helpers must be compiled with `-std=c99` (or
`-std=c11`). Mixing C++ compilation flags with C sources can cause issues. CMake
enforces `C_STANDARD 99` on all C targets automatically; when compiling manually,
pass `-std=c99` explicitly.

### cmake --build fails after editing CMakeLists.txt

Re-run configure first:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
```
