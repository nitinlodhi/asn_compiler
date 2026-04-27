#include "runtime/uper/UperExtension.h"
#include "runtime/uper/UperLength.h"
#include "runtime/core/BitString.h"

namespace asn1::runtime {

void UperExtension::encodeExtensionMarker(BitWriter& writer, bool isExtended) {
    writer.writeBits(isExtended ? 1 : 0, 1);
}

bool UperExtension::decodeExtensionMarker(BitReader& reader) {
    return reader.readBits(1) != 0;
}

void UperExtension::encodeOpenType(BitWriter& writer, const BitString& open_type_data) {
    size_t octetLength = (open_type_data.bit_length + 7) / 8;
    UperLength::encodeUnconstrainedLength(writer, octetLength);
    writer.alignToOctet();
    writer.writeBytes(open_type_data.data.data(), open_type_data.bit_length);
}

BitString UperExtension::decodeOpenType(BitReader& reader) {
    size_t octetLength = UperLength::decodeUnconstrainedLength(reader);
    reader.alignToOctet();
    BitString open_type_data;
    open_type_data.bit_length = octetLength * 8; // The length is in octets for open types
    reader.readBytes(open_type_data.data, open_type_data.bit_length);
    return open_type_data;
}

} // namespace asn1::runtime
