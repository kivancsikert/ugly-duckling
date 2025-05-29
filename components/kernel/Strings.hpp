#pragma once

#include <string>
#include <stdint.h>

namespace farmhub::kernel {

static constexpr const char* DIGITS = "0123456789abcdef";

std::string toHexString(uint64_t value) {
    char buffer[17] = {0};

    for (int i = 15; i >= 0; --i) {
        buffer[i] = DIGITS[value & 0xF];
        value >>= 4;
    }

    const char* start = buffer;
    while (*start == '0' && *(start + 1) != '\0') {
        ++start;
    }

    return std::string(start);
}

std::string toStringWithPrecision(double value, int precision) {
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "%.*f", precision, value);
    return std::string(buffer);
}

}    // namespace farmhub::kernel
