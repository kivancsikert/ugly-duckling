#pragma once

#include <string>

namespace farmhub::peripherals::api {

struct IPeripheral {
    virtual ~IPeripheral() = default;

    virtual const std::string& getName() const = 0;
};

} // namespace farmhub::peripherals::api
