# Codegen: language-agnostic infrastructure + language-specific backends

# Language-agnostic base: type mapping, formatting, template management
add_library(codegen_base STATIC
    TypeMap.cpp
    Formatter.cpp
    TemplateManager.cpp
)
target_include_directories(codegen_base PUBLIC ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(codegen_base PRIVATE frontend)

# Language-specific backends
add_subdirectory(cpp)
add_subdirectory(c)
# add_subdirectory(java)    # uncomment when Java backend is added
# add_subdirectory(python)  # uncomment when Python backend is added
