#include "runtime/uper/UperSequence.h"

namespace asn1::runtime {

void UperSequence::encodeSequencePreamble(BitWriter& writer, uint64_t presenceBitmap, 
                                         int optionalFieldCount) {
    if (optionalFieldCount > 0) {
        writer.writeBits(presenceBitmap, optionalFieldCount);
    }
}

uint64_t UperSequence::decodeSequencePreamble(BitReader& reader, int optionalFieldCount) {
    if (optionalFieldCount <= 0) {
        return 0;
    }
    return reader.readBits(optionalFieldCount);
}

void UperSequence::encodeExtensionAddition(BitWriter& writer, bool hasExtensions) {
    writer.writeBits(hasExtensions ? 1 : 0, 1);
}

bool UperSequence::decodeExtensionAddition(BitReader& reader) {
    return reader.readBits(1) != 0;
}

} // namespace asn1::runtime
