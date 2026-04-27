cmake_minimum_required(VERSION 3.20)
project(ASN1Compiler
    VERSION 1.0.0
    DESCRIPTION "3GPP ASN.1 Parser & UPER/PER Codec (C++)"
    LANGUAGES CXX
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
    codegen_base
    codegen_cpp
    utils_core
    utils_testing
)

# ── Runtime convenience library (combined, for linking with generated code) ──
add_library(asn1_runtime STATIC
    $<TARGET_OBJECTS:runtime_core>
    $<TARGET_OBJECTS:runtime_uper>
)
target_include_directories(asn1_runtime PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)

# ── ASN.1 schemas → generated headers + static libraries ────────────────────
set(ASN1_SCHEMAS
    schemas/nr-rrc-15.6.0.asn1
)

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

# ── Tests ────────────────────────────────────────────────────────────────────
enable_testing()
add_subdirectory(tests)

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
