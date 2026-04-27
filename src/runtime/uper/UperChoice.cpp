#include "runtime/uper/UperChoice.h"
#include "runtime/uper/RangeUtils.h"

namespace asn1::runtime {

void UperChoice::encodeChoiceIndex(BitWriter& writer, int choiceIndex, int numChoices) {
    if (numChoices <= 1) {
        return;
    }
    
    int bits = RangeUtils::calculateRangeBits(0, numChoices - 1);
    writer.writeBits(choiceIndex, bits);
}

int UperChoice::decodeChoiceIndex(BitReader& reader, int numChoices) {
    if (numChoices <= 1) {
        return 0;
    }
    
    int bits = RangeUtils::calculateRangeBits(0, numChoices - 1);
    return reader.readBits(bits);
}

void UperChoice::encodeExtendedChoiceIndex(BitWriter& writer, int choiceIndex, int numBaseChoices) {
    if (choiceIndex < numBaseChoices) {
        writer.writeBits(0, 1); // Not extended
        encodeChoiceIndex(writer, choiceIndex, numBaseChoices);
    } else {
        writer.writeBits(1, 1); // Extended
        // TODO: Encode extended choice
    }
}

int UperChoice::decodeExtendedChoiceIndex(BitReader& reader, int numBaseChoices) {
    bool isExtended = reader.readBits(1) != 0;
    if (!isExtended) {
        return decodeChoiceIndex(reader, numBaseChoices);
    }
    // TODO: Decode extended choice
    return -1;
}

} // namespace asn1::runtime
