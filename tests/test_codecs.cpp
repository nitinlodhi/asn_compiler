#include "tests.h"
#include "runtime/core/BitWriter.h"
#include "runtime/core/BitReader.h"
#include "runtime/uper/UperLength.h"
#include "runtime/uper/UperInteger.h"
#include "runtime/uper/UperObjectIdentifier.h"
#include "runtime/uper/UperReal.h"
#include "runtime/core/ObjectIdentifier.h"
#include <cassert>
#include <cstring>

using namespace asn1;

bool testUperObjectIdentifier() {
    runtime::ObjectIdentifier oid = {1, 2, 840, 113549};
    
    runtime::BitWriter writer;
    runtime::UperObjectIdentifier::encode(writer, oid);

    // Expected encoding for {1 2 840 113549}
    // 1*40+2 = 42 -> 0x2A
    // 840 -> 0x86 0x48
    // 113549 -> 0x86 0xF7 0x0D
    // content: 2A 86 48 86 F7 0D
    // length: 6 -> 0x06
    // total: 06 2A 86 48 86 F7 0D
    
    const uint8_t expected[] = {0x06, 0x2A, 0x86, 0x48, 0x86, 0xF7, 0x0D};
    assert(writer.getBufferSize() == sizeof(expected));
    assert(memcmp(writer.getBuffer(), expected, sizeof(expected)) == 0);

    runtime::BitReader reader(writer.getBuffer(), writer.getBufferSize());
    runtime::ObjectIdentifier decoded_oid;
    runtime::UperObjectIdentifier::decode(reader, decoded_oid);

    assert(decoded_oid.size() == oid.size());
    assert(decoded_oid == oid);

    return true;
}

bool testUperUnconstrainedLength() {
    runtime::BitWriter writer;
    // Test length < 128
    runtime::UperLength::encodeUnconstrainedLength(writer, 10);
    // Test length >= 128
    runtime::UperLength::encodeUnconstrainedLength(writer, 500);

    runtime::BitReader reader(writer.getBuffer(), writer.getBufferSize());
    size_t len1 = runtime::UperLength::decodeUnconstrainedLength(reader);
    assert(len1 == 10);
    size_t len2 = runtime::UperLength::decodeUnconstrainedLength(reader);
    assert(len2 == 500);

    // Test boundary
    runtime::BitWriter writer2;
    runtime::UperLength::encodeUnconstrainedLength(writer2, 127);
    runtime::UperLength::encodeUnconstrainedLength(writer2, 128);
    runtime::BitReader reader2(writer2.getBuffer(), writer2.getBufferSize());
    assert(runtime::UperLength::decodeUnconstrainedLength(reader2) == 127);
    assert(runtime::UperLength::decodeUnconstrainedLength(reader2) == 128);

    return true;
}

bool testUperFragmentationLength() {
    runtime::BitWriter writer;
    // Test length >= 16384
    size_t large_length = 20000;
    runtime::UperLength::encodeUnconstrainedLength(writer, large_length);

    runtime::BitReader reader(writer.getBuffer(), writer.getBufferSize());
    // The decoder should return the size of the block, not the original length
    size_t m = (large_length + 16383) / 16384; // should be 2
    size_t expected_decoded_length = m * 16384; // should be 32768
    
    size_t decoded_length = runtime::UperLength::decodeUnconstrainedLength(reader);
    assert(decoded_length == expected_decoded_length);

    // Test boundary
    runtime::BitWriter writer2;
    runtime::UperLength::encodeUnconstrainedLength(writer2, 16383); // should be 2-octet
    runtime::UperLength::encodeUnconstrainedLength(writer2, 16384); // should be fragmented
    runtime::BitReader reader2(writer2.getBuffer(), writer2.getBufferSize());
    assert(runtime::UperLength::decodeUnconstrainedLength(reader2) == 16383);
    assert(runtime::UperLength::decodeUnconstrainedLength(reader2) == 16384); // m=1, returns 1*16384

    return true;
}

bool testUperUnconstrainedInteger() {
    // Test case 1: 0
    runtime::BitWriter writer1;
    runtime::UperInteger::encodeUnconstrainedInt(writer1, 0);
    runtime::BitReader reader1(writer1.getBuffer(), writer1.getBufferSize());
    assert(runtime::UperInteger::decodeUnconstrainedInt(reader1) == 0);

    // Test case 2: 127
    runtime::BitWriter writer2;
    runtime::UperInteger::encodeUnconstrainedInt(writer2, 127);
    runtime::BitReader reader2(writer2.getBuffer(), writer2.getBufferSize());
    assert(runtime::UperInteger::decodeUnconstrainedInt(reader2) == 127);

    // Test case 3: 128
    runtime::BitWriter writer3;
    runtime::UperInteger::encodeUnconstrainedInt(writer3, 128);
    runtime::BitReader reader3(writer3.getBuffer(), writer3.getBufferSize());
    assert(runtime::UperInteger::decodeUnconstrainedInt(reader3) == 128);

    // Test case 4: -128
    runtime::BitWriter writer4;
    runtime::UperInteger::encodeUnconstrainedInt(writer4, -128);
    runtime::BitReader reader4(writer4.getBuffer(), writer4.getBufferSize());
    assert(runtime::UperInteger::decodeUnconstrainedInt(reader4) == -128);

    // Test case 5: -129
    runtime::BitWriter writer5;
    runtime::UperInteger::encodeUnconstrainedInt(writer5, -129);
    runtime::BitReader reader5(writer5.getBuffer(), writer5.getBufferSize());
    assert(runtime::UperInteger::decodeUnconstrainedInt(reader5) == -129);

    // Test case 6: Large positive
    runtime::BitWriter writer6;
    runtime::UperInteger::encodeUnconstrainedInt(writer6, 32768);
    runtime::BitReader reader6(writer6.getBuffer(), writer6.getBufferSize());
    assert(runtime::UperInteger::decodeUnconstrainedInt(reader6) == 32768);

    // Test case 7: Large negative
    runtime::BitWriter writer7;
    runtime::UperInteger::encodeUnconstrainedInt(writer7, -32769);
    runtime::BitReader reader7(writer7.getBuffer(), writer7.getBufferSize());
    assert(runtime::UperInteger::decodeUnconstrainedInt(reader7) == -32769);
    
    return true;
}

bool testUperNormallySmallInteger() {
    // Test small value
    runtime::BitWriter writer1;
    runtime::UperInteger::encodeNormallySmallInt(writer1, 42);
    runtime::BitReader reader1(writer1.getBuffer(), writer1.getBufferSize());
    assert(runtime::UperInteger::decodeNormallySmallInt(reader1) == 42);

    // Test large value
    runtime::BitWriter writer2;
    runtime::UperInteger::encodeNormallySmallInt(writer2, 1000);
    runtime::BitReader reader2(writer2.getBuffer(), writer2.getBufferSize());
    assert(runtime::UperInteger::decodeNormallySmallInt(reader2) == 1000);

    // Test boundary
    runtime::BitWriter writer3;
    runtime::UperInteger::encodeNormallySmallInt(writer3, 63);
    runtime::UperInteger::encodeNormallySmallInt(writer3, 64);
    runtime::BitReader reader3(writer3.getBuffer(), writer3.getBufferSize());
    assert(runtime::UperInteger::decodeNormallySmallInt(reader3) == 63);
    assert(runtime::UperInteger::decodeNormallySmallInt(reader3) == 64);

    return true;
}

bool testUperReal() {
    runtime::BitWriter writer;
    double val1 = 123.456;
    double val2 = -0.789;
    
    runtime::UperReal::encode(writer, val1);
    runtime::UperReal::encode(writer, val2);

    runtime::BitReader reader(writer.getBuffer(), writer.getBufferSize());
    double decoded_val1, decoded_val2;
    runtime::UperReal::decode(reader, decoded_val1);
    runtime::UperReal::decode(reader, decoded_val2);

    assert(decoded_val1 == val1);
    assert(decoded_val2 == val2);
    return true;
}

void register_runtime_codecs_tests(utils::TestFramework& runner) {
    runner.addTest("UPER Runtime: OBJECT IDENTIFIER", testUperObjectIdentifier);
    runner.addTest("UPER Runtime: Unconstrained Length", testUperUnconstrainedLength);
    runner.addTest("UPER Runtime: Fragmentation Length", testUperFragmentationLength);
    runner.addTest("UPER Runtime: Unconstrained Integer", testUperUnconstrainedInteger);
    runner.addTest("UPER Runtime: Normally Small Integer", testUperNormallySmallInteger);
    runner.addTest("UPER Runtime: REAL", testUperReal);
}