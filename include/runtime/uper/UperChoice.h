#ifndef ASN1_RUNTIME_UPER_CHOICE_H
#define ASN1_RUNTIME_UPER_CHOICE_H

#include <cstdint>
#include "runtime/core/BitWriter.h"
#include "runtime/core/BitReader.h"

namespace asn1::runtime {

class UperChoice {
public:
    // Encode/Decode CHOICE index
    static void encodeChoiceIndex(BitWriter& writer, int choiceIndex, int numChoices);
    static int decodeChoiceIndex(BitReader& reader, int numChoices);
    
    // Extension-aware choice (handles ... in CHOICE)
    static void encodeExtendedChoiceIndex(BitWriter& writer, int choiceIndex, int numBaseChoices);
    static int decodeExtendedChoiceIndex(BitReader& reader, int numBaseChoices);
};

} // namespace asn1::runtime

#endif // ASN1_RUNTIME_UPER_CHOICE_H
