# Codegen C backend: CEmitter (type definitions) + CCodecEmitter (encode_*/decode_*)

add_library(codegen_c STATIC
    CEmitter.cpp
    CCodecEmitter.cpp
)
target_include_directories(codegen_c PUBLIC ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(codegen_c PRIVATE codegen_base frontend)
