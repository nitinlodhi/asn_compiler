# Tests Configuration

add_executable(run_tests
    main.cpp
    TestFramework.cpp
    test_bitstream.cpp
    test_codecs.cpp
    test_lexer.cpp
    test_parser.cpp
    test_resolver.cpp
    test_utils.cpp
)

target_include_directories(run_tests PRIVATE ${CMAKE_SOURCE_DIR}/include ${CMAKE_SOURCE_DIR}/tests)
target_link_libraries(run_tests PRIVATE
    frontend
    runtime_core
    runtime_uper
    codegen_base
    codegen_cpp
    utils_core
    utils_testing
)

add_test(NAME AllTests COMMAND run_tests)
