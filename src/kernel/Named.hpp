#pragma once

#include <Arduino.h>

namespace farmhub::kernel {

class Named {
protected:
    Named(const String& name)
        : name(name) {
    }

public:
    const String name;
};

}    // namespace farmhub::kernel
