add_library(runtime_aper
    AperInteger.cpp
)

target_include_directories(runtime_aper PUBLIC
    ${PROJECT_SOURCE_DIR}/include
)

target_link_libraries(runtime_aper PUBLIC runtime_core runtime_uper)
