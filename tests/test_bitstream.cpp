#include "tests.h"
#include "runtime/core/BitWriter.h"
#include "runtime/core/BitReader.h"
#include <cassert>

using namespace asn1;

bool testBitStream() {
    runtime::BitWriter writer;
    writer.writeBits(5, 3); // Write 101 (5 in 3 bits)
    writer.writeBits(2, 2); // Write 10 (2 in 2 bits)

    runtime::BitReader reader(writer.getBuffer(), writer.getBufferSize());
    uint64_t val1 = reader.readBits(3);
    uint64_t val2 = reader.readBits(2);

    assert(val1 == 5);
    assert(val2 == 2);
    return true;
}

void register_runtime_bitstream_tests(utils::TestFramework& runner) {
    runner.addTest("BitStream: Write and Read", testBitStream);
}