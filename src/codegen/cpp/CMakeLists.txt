# Codegen C++ backend: type emitter + codec function emitter

add_library(codegen_cpp STATIC
    CppEmitter.cpp
    CodecEmitter.cpp
)
target_include_directories(codegen_cpp PUBLIC ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(codegen_cpp PRIVATE codegen_base frontend runtime_uper)
