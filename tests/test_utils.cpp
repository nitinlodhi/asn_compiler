#include "tests.h"
#include "runtime/uper/RangeUtils.h"
#include <cassert>

using namespace asn1;

bool testRangeUtils() {
    auto range = runtime::RangeUtils::extractRange("0..255");
    assert(range.has_value());
    assert(range->first == 0);
    assert(range->second == 255);
    return true;
}

void register_runtime_utils_tests(utils::TestFramework& runner) {
    runner.addTest("RangeUtils: Extract Range", testRangeUtils);
}