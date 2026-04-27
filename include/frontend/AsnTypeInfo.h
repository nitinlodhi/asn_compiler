#ifndef ASN1_FRONTEND_ASN_TYPE_INFO_H
#define ASN1_FRONTEND_ASN_TYPE_INFO_H

#include <string>
#include <memory>
#include <optional>

namespace asn1::frontend {

class AsnTypeInfo {
public:
    std::string typeName;
    std::string baseType;
    bool isFixedSize;
    std::optional<int> minBits;
    std::optional<int> maxBits;
    std::optional<long long> minValue;
    std::optional<long long> maxValue;
    bool hasExtension;

    AsnTypeInfo(const std::string& typeName);
    ~AsnTypeInfo() = default;

    void setFixedSize(bool fixed, int bits);
    void setRangeConstraint(long long minVal, long long maxVal);
    int calculateBitWidth() const;
    std::string toString() const;
};

using AsnTypeInfoPtr = std::shared_ptr<AsnTypeInfo>;

} // namespace asn1::frontend

#endif // ASN1_FRONTEND_ASN_TYPE_INFO_H
