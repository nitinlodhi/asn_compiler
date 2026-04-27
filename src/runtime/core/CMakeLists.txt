# Runtime core: bitstream primitives (BitReader, BitWriter, BitUtils)

add_library(runtime_core OBJECT
    BitReader.cpp
    BitWriter.cpp
    BitUtils.cpp
)
target_include_directories(runtime_core PUBLIC ${CMAKE_SOURCE_DIR}/include)
