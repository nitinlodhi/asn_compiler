cmake_minimum_required(VERSION 3.20)
project(ASN1Compiler
    VERSION 1.0.0
    DESCRIPTION "3GPP ASN.1 Parser & UPER/PER Codec (C++/C)"
    LANGUAGES CXX C
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

# Single canonical include root — no build-dir hacks needed
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include)

if(MSVC)
    add_compile_options(/W4)
else()
    add_compile_options(-Wall -Wextra -Wpedantic -Wno-unused-parameter)
endif()

# ── Internal compiler ────────────────────────────────────────────────────────
add_subdirectory(src/frontend)
add_subdirectory(src/runtime)
add_subdirectory(src/codegen)
add_subdirectory(src/utils)

add_executable(asn1_compiler src/utils/CompilerMain.cpp)
target_include_directories(asn1_compiler PRIVATE include)
target_link_libraries(asn1_compiler PRIVATE
    frontend
    runtime_core
    runtime_uper
    runtime_aper
    codegen_base
    codegen_cpp
    codegen_c
    utils_core
    utils_testing
)

# ── C++ runtime convenience library (combined, for linking with generated C++ code) ──
add_library(asn1_runtime STATIC
    $<TARGET_OBJECTS:runtime_core>
    $<TARGET_OBJECTS:runtime_uper>
    $<TARGET_OBJECTS:runtime_aper>
)
target_include_directories(asn1_runtime PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)

# ── ASN.1 schemas list (shared by C++ and C build rules) ─────────────────────
set(ASN1_SCHEMAS
    schemas/nr-rrc-15.6.0.asn1
)

# ── APER schemas — compiled with --encoding aper ──────────────────────────────
set(APER_SCHEMAS
    schemas/e1ap_15.4.asn1
)

# ── C schemas → generated headers + static C libraries ───────────────────────
set(GENERATED_C_DIR ${CMAKE_BINARY_DIR}/generated_c)
file(MAKE_DIRECTORY ${GENERATED_C_DIR})

foreach(SCHEMA ${ASN1_SCHEMAS})
    get_filename_component(_SCHEMA_FULL_NAME ${SCHEMA} NAME)
    string(REPLACE ".asn1" "" _SCHEMA_STEM ${_SCHEMA_FULL_NAME})
    string(REGEX REPLACE "[^A-Za-z0-9]" "_" LIB_NAME ${_SCHEMA_STEM})

    set(C_LIB_NAME   ${LIB_NAME}_c)
    set(GEN_C_H      ${GENERATED_C_DIR}/${LIB_NAME}.h)
    set(GEN_C_SRC    ${GENERATED_C_DIR}/${LIB_NAME}.c)

    add_custom_command(
        OUTPUT  ${GEN_C_H} ${GEN_C_SRC}
        COMMAND $<TARGET_FILE:asn1_compiler>
                ${CMAKE_CURRENT_SOURCE_DIR}/${SCHEMA}
                -o ${GENERATED_C_DIR}/${LIB_NAME}
                --lang c
        DEPENDS asn1_compiler ${CMAKE_CURRENT_SOURCE_DIR}/${SCHEMA}
        COMMENT "Compiling ASN.1 schema (C): ${SCHEMA}"
    )

    add_library(${C_LIB_NAME} STATIC ${GEN_C_SRC})
    set_target_properties(${C_LIB_NAME} PROPERTIES LANGUAGE C C_STANDARD 99)
    target_include_directories(${C_LIB_NAME} PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${GENERATED_C_DIR}
    )
    target_link_libraries(${C_LIB_NAME} PUBLIC runtime_c)
endforeach()

# ── ASN.1 schemas → generated C++ headers + static libraries ─────────────────

set(GENERATED_DIR ${CMAKE_BINARY_DIR}/generated)
file(MAKE_DIRECTORY ${GENERATED_DIR})

set(DIST_INCLUDE_DIR ${CMAKE_BINARY_DIR}/dist/include)
set(DIST_LIB_DIR     ${CMAKE_BINARY_DIR}/dist/lib)
file(MAKE_DIRECTORY ${DIST_INCLUDE_DIR}/runtime ${DIST_LIB_DIR})

foreach(SCHEMA ${ASN1_SCHEMAS})
    # Derive a safe C identifier from the schema filename (strips .asn1 then
    # replaces every non-alphanumeric character with underscore).
    get_filename_component(_SCHEMA_FULL_NAME ${SCHEMA} NAME)   # nr-rrc-15.6.0.asn1
    string(REPLACE ".asn1" "" _SCHEMA_STEM ${_SCHEMA_FULL_NAME})       # nr-rrc-15.6.0
    string(REGEX REPLACE "[^A-Za-z0-9]" "_" LIB_NAME ${_SCHEMA_STEM}) # nr_rrc_15_6_0

    set(GEN_H   ${GENERATED_DIR}/${LIB_NAME}.h)
    set(GEN_CPP ${GENERATED_DIR}/${LIB_NAME}.cpp)

    # Run the compiler to generate .h + .cpp
    add_custom_command(
        OUTPUT  ${GEN_H} ${GEN_CPP}
        COMMAND $<TARGET_FILE:asn1_compiler>
                ${CMAKE_CURRENT_SOURCE_DIR}/${SCHEMA}
                -o ${GENERATED_DIR}/${LIB_NAME}
        DEPENDS asn1_compiler ${CMAKE_CURRENT_SOURCE_DIR}/${SCHEMA}
        COMMENT "Compiling ASN.1 schema: ${SCHEMA}"
    )

    # Build a static library: generated codec + runtime
    add_library(${LIB_NAME} STATIC ${GEN_CPP})
    target_include_directories(${LIB_NAME} PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include   # runtime/ headers
        ${GENERATED_DIR}                      # generated .h itself
    )
    target_link_libraries(${LIB_NAME} PUBLIC asn1_runtime)

    # Copy artefacts into dist/ after the library is built
    add_custom_command(TARGET ${LIB_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy ${GEN_H} ${DIST_INCLUDE_DIR}/${LIB_NAME}.h
        COMMENT "Staging ${LIB_NAME}.h → dist/include"
    )
    add_custom_command(TARGET ${LIB_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy
            $<TARGET_FILE:${LIB_NAME}> ${DIST_LIB_DIR}/lib${LIB_NAME}.a
        COMMENT "Staging lib${LIB_NAME}.a → dist/lib"
    )
endforeach()

# Copy libasn1_runtime.a into dist/lib/ after it is built
add_custom_command(TARGET asn1_runtime POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:asn1_runtime> ${DIST_LIB_DIR}/libasn1_runtime.a
    COMMENT "Staging libasn1_runtime.a → dist/lib"
)

# Copy runtime headers into dist/include/runtime/ once (depend on any schema lib)
add_custom_target(dist_runtime_headers ALL
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_CURRENT_SOURCE_DIR}/include/runtime
        ${DIST_INCLUDE_DIR}/runtime
    COMMENT "Staging runtime headers → dist/include/runtime"
)

# ── APER schemas → generated C++ headers + static libraries ──────────────────

set(GENERATED_APER_DIR ${CMAKE_BINARY_DIR}/generated_aper)
file(MAKE_DIRECTORY ${GENERATED_APER_DIR})

foreach(SCHEMA ${APER_SCHEMAS})
    get_filename_component(_SCHEMA_FULL_NAME ${SCHEMA} NAME)
    string(REPLACE ".asn1" "" _SCHEMA_STEM ${_SCHEMA_FULL_NAME})
    string(REGEX REPLACE "[^A-Za-z0-9]" "_" LIB_NAME ${_SCHEMA_STEM})

    set(APER_LIB_NAME ${LIB_NAME}_aper)
    set(GEN_APER_H   ${GENERATED_APER_DIR}/${LIB_NAME}.h)
    set(GEN_APER_CPP ${GENERATED_APER_DIR}/${LIB_NAME}.cpp)

    add_custom_command(
        OUTPUT  ${GEN_APER_H} ${GEN_APER_CPP}
        COMMAND $<TARGET_FILE:asn1_compiler>
                ${CMAKE_CURRENT_SOURCE_DIR}/${SCHEMA}
                -o ${GENERATED_APER_DIR}/${LIB_NAME}
                --encoding aper
        DEPENDS asn1_compiler ${CMAKE_CURRENT_SOURCE_DIR}/${SCHEMA}
        COMMENT "Compiling ASN.1 schema (APER): ${SCHEMA}"
    )

    add_library(${APER_LIB_NAME} STATIC ${GEN_APER_CPP})
    target_include_directories(${APER_LIB_NAME} PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${GENERATED_APER_DIR}
    )
    target_link_libraries(${APER_LIB_NAME} PUBLIC asn1_runtime)
endforeach()

# ── Tests ────────────────────────────────────────────────────────────────────
enable_testing()
add_subdirectory(tests)

# ── Schema integration tests (one executable per schema) ─────────────────────
# Each tests/<schema-stem>/ directory is expected to contain a
# test_<lib_name>.cpp file that links against the generated schema library.
foreach(SCHEMA ${ASN1_SCHEMAS})
    get_filename_component(_SCHEMA_FULL_NAME ${SCHEMA} NAME)
    string(REPLACE ".asn1" "" _SCHEMA_STEM ${_SCHEMA_FULL_NAME})
    string(REGEX REPLACE "[^A-Za-z0-9]" "_" LIB_NAME ${_SCHEMA_STEM})

    set(_SCHEMA_TEST_SRC
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/${_SCHEMA_STEM}/test_${LIB_NAME}.cpp)

    if(EXISTS ${_SCHEMA_TEST_SRC})
        set(_TEST_TARGET schema_tests_${LIB_NAME})
        add_executable(${_TEST_TARGET} ${_SCHEMA_TEST_SRC})
        target_include_directories(${_TEST_TARGET} PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/include
            ${GENERATED_DIR}
        )
        target_link_libraries(${_TEST_TARGET} PRIVATE ${LIB_NAME})
        add_test(NAME SchemaTests_${LIB_NAME} COMMAND ${_TEST_TARGET})
        message(STATUS "  Schema test   : ${_TEST_TARGET}")
    endif()
endforeach()

# ── C schema integration tests (one C executable per schema) ─────────────────
foreach(SCHEMA ${ASN1_SCHEMAS})
    get_filename_component(_SCHEMA_FULL_NAME ${SCHEMA} NAME)
    string(REPLACE ".asn1" "" _SCHEMA_STEM ${_SCHEMA_FULL_NAME})
    string(REGEX REPLACE "[^A-Za-z0-9]" "_" LIB_NAME ${_SCHEMA_STEM})

    set(_C_TEST_SRC
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/${_SCHEMA_STEM}/test_${LIB_NAME}_c.c)

    if(EXISTS ${_C_TEST_SRC})
        set(_C_TEST_TARGET schema_tests_${LIB_NAME}_c)
        add_executable(${_C_TEST_TARGET} ${_C_TEST_SRC})
        set_target_properties(${_C_TEST_TARGET} PROPERTIES LANGUAGE C C_STANDARD 99)
        target_include_directories(${_C_TEST_TARGET} PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/include
            ${GENERATED_C_DIR}
        )
        target_link_libraries(${_C_TEST_TARGET} PRIVATE ${LIB_NAME}_c)
        add_test(NAME SchemaTests_${LIB_NAME}_c COMMAND ${_C_TEST_TARGET})
        message(STATUS "  C schema test : ${_C_TEST_TARGET}")
    endif()
endforeach()

# ── APER schema integration tests ────────────────────────────────────────────
# Each tests/<schema-stem>/ directory may contain test_<lib_name>_aper.cpp.
foreach(SCHEMA ${APER_SCHEMAS})
    get_filename_component(_SCHEMA_FULL_NAME ${SCHEMA} NAME)
    string(REPLACE ".asn1" "" _SCHEMA_STEM ${_SCHEMA_FULL_NAME})
    string(REGEX REPLACE "[^A-Za-z0-9]" "_" LIB_NAME ${_SCHEMA_STEM})

    set(APER_LIB_NAME ${LIB_NAME}_aper)
    set(_APER_TEST_SRC
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/${_SCHEMA_STEM}/test_${LIB_NAME}_aper.cpp)

    if(EXISTS ${_APER_TEST_SRC})
        set(_APER_TEST_TARGET schema_tests_${LIB_NAME}_aper)
        add_executable(${_APER_TEST_TARGET} ${_APER_TEST_SRC})
        target_include_directories(${_APER_TEST_TARGET} PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/include
            ${GENERATED_APER_DIR}
        )
        target_link_libraries(${_APER_TEST_TARGET} PRIVATE ${APER_LIB_NAME})
        add_test(NAME SchemaTests_${LIB_NAME}_aper COMMAND ${_APER_TEST_TARGET})
        message(STATUS "  APER schema test : ${_APER_TEST_TARGET}")
    endif()
endforeach()

# ── Install (optional `cmake --install`) ─────────────────────────────────────
install(TARGETS asn1_compiler RUNTIME DESTINATION bin)
install(DIRECTORY ${DIST_INCLUDE_DIR}/ DESTINATION dist/include)
install(DIRECTORY ${DIST_LIB_DIR}/     DESTINATION dist/lib)

message(STATUS "ASN.1 Compiler Configuration")
message(STATUS "  C++ Standard : ${CMAKE_CXX_STANDARD}")
message(STATUS "  Build Type   : ${CMAKE_BUILD_TYPE}")
message(STATUS "  Source Dir   : ${CMAKE_SOURCE_DIR}")
message(STATUS "  Binary Dir   : ${CMAKE_BINARY_DIR}")
message(STATUS "  Schemas      : ${ASN1_SCHEMAS}")
message(STATUS "  dist/        : ${CMAKE_BINARY_DIR}/dist")
