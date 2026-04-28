# C runtime: BitWriter, BitReader, UPER codec primitives (pure C)

add_library(runtime_c STATIC
    asn1_bitwriter.c
    asn1_bitreader.c
    asn1_uper.c
)
set_target_properties(runtime_c PROPERTIES LANGUAGE C C_STANDARD 99)
target_include_directories(runtime_c PUBLIC ${CMAKE_SOURCE_DIR}/include)
