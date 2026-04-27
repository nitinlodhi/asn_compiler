#include "utils/CompareUtils.h"
#include <iomanip>
#include <sstream>
#include <cstring>

namespace asn1::utils {

bool CompareUtils::compareBuffers(const uint8_t* expected, const uint8_t* actual, size_t length) {
    return std::memcmp(expected, actual, length) == 0;
}

std::string CompareUtils::bufferToHex(const uint8_t* buffer, size_t length) {
    std::stringstream ss;
    for (size_t i = 0; i < length; i++) {
        ss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(buffer[i]);
    }
    return ss.str();
}

bool CompareUtils::hexStringToBuffer(const std::string& hex, uint8_t* buffer, size_t& length) {
    if (hex.length() % 2 != 0) {
        return false;
    }
    
    length = hex.length() / 2;
    for (size_t i = 0; i < length; i++) {
        std::string byteStr = hex.substr(i * 2, 2);
        try {
            buffer[i] = static_cast<uint8_t>(std::stoi(byteStr, nullptr, 16));
        } catch (...) {
            return false;
        }
    }
    
    return true;
}

} // namespace asn1::utils
