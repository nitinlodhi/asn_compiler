#ifndef ASN1_RUNTIME_RANGE_UTILS_H
#define ASN1_RUNTIME_RANGE_UTILS_H

#include <cstdint>
#include <optional>

namespace asn1::runtime {

class RangeUtils {
public:
    static int calculateRangeBits(int64_t minValue, int64_t maxValue);
    static int64_t normalizeValue(int64_t value, int64_t minValue);
    static int64_t denormalizeValue(int64_t normalized, int64_t minValue);
    static std::optional<std::pair<int64_t, int64_t>> extractRange(const std::string& constraint);
};

} // namespace asn1::runtime

#endif // ASN1_RUNTIME_RANGE_UTILS_H
