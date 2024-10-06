#pragma once

#include <Arduino.h>

namespace farmhub::kernel {

class StringPrint : public Print {
public:
    String buffer;

    virtual size_t write(uint8_t byte) {
        buffer += (char) byte;
        return 1;
    }

    virtual size_t write(const uint8_t* buffer, size_t size) {
        for (size_t i = 0; i < size; i++) {
            write(buffer[i]);
        }
        return size;
    }

    void clear() {
        buffer = "";
    }
};

}    // namespace farmhub::kernel
