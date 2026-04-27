#pragma once

#include "utils/TestFramework.h"

// Forward declarations for test registration functions
void register_lexer_tests(asn1::utils::TestFramework& runner);
void register_parser_tests(asn1::utils::TestFramework& runner);
void register_resolver_tests(asn1::utils::TestFramework& runner);
void register_runtime_bitstream_tests(asn1::utils::TestFramework& runner);
void register_runtime_codecs_tests(asn1::utils::TestFramework& runner);
void register_runtime_utils_tests(asn1::utils::TestFramework& runner);

namespace asn1::tests {
    class ThreeGppHexTests;
}