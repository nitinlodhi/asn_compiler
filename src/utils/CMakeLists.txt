# Utils: Infrastructure, CLI, and Validation

# Infrastructure Libraries
add_library(utils_core STATIC
    FileLoader.cpp
    Logger.cpp
)
target_include_directories(utils_core PUBLIC ${CMAKE_SOURCE_DIR}/include)

# Testing & Validation
add_library(utils_testing STATIC
    TestFramework.cpp
    CompareUtils.cpp
)
target_include_directories(utils_testing PUBLIC ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(utils_testing PRIVATE
    frontend
    runtime_core runtime_uper
    codegen_base codegen_cpp
)
