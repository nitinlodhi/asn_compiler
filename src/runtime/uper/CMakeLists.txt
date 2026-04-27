# Runtime UPER: Unaligned Packed Encoding Rules codec + structural helpers

add_library(runtime_uper OBJECT
    RangeUtils.cpp
    UperInteger.cpp
    UperLength.cpp
    UperReal.cpp
    UperChoice.cpp
    UperExtension.cpp
    UperObjectIdentifier.cpp
    UperSequence.cpp
)
target_include_directories(runtime_uper PUBLIC ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(runtime_uper PUBLIC runtime_core)
