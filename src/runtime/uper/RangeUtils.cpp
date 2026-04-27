#include "runtime/uper/RangeUtils.h"
#include <cmath>
#include <string>

namespace asn1::runtime {

int RangeUtils::calculateRangeBits(int64_t minValue, int64_t maxValue) {
    if (minValue >= maxValue) {
        return 0;
    }
    
    int64_t range = maxValue - minValue + 1;
    if (range <= 0) return 0;
    
    return static_cast<int>(std::ceil(std::log2(range)));
}

int64_t RangeUtils::normalizeValue(int64_t value, int64_t minValue) {
    return value - minValue;
}

int64_t RangeUtils::denormalizeValue(int64_t normalized, int64_t minValue) {
    return normalized + minValue;
}

std::optional<std::pair<int64_t, int64_t>> RangeUtils::extractRange(const std::string& constraint) {
    size_t dotdotPos = constraint.find("..");
    if (dotdotPos == std::string::npos) {
        return std::nullopt;
    }

    std::string minStr = constraint.substr(0, dotdotPos);
    std::string maxStr = constraint.substr(dotdotPos + 2);

    try {
        int64_t minVal = std::stoll(minStr);
        int64_t maxVal = std::stoll(maxStr);
        return std::make_pair(minVal, maxVal);
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

} // namespace asn1::runtime
