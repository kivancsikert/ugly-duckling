#pragma once

#include <chrono>

#include <kernel/Configuration.hpp>
#include <kernel/I2CManager.hpp>

using namespace std::chrono;
using namespace farmhub::kernel;

namespace farmhub::peripherals {

class I2CDeviceConfig
    : public ConfigurationSection {
public:
    // I2C address is typically a hexadecimal number,
    // but JSON doesn't support 0x notation, so we
    // take it as a string instead
    Property<std::string> address { this, "address" };
    Property<InternalPinPtr> sda { this, "sda" };
    Property<InternalPinPtr> scl { this, "scl" };

    I2CConfig parse(uint8_t defaultAddress = 0xFF, InternalPinPtr defaultSda = nullptr, InternalPinPtr defaultScl = nullptr) const {
        return {
            address.get().empty()
                ? defaultAddress
                : (uint8_t) strtol(address.get().c_str(), nullptr, 0),
            sda.get() == nullptr
                ? defaultSda
                : sda.get(),
            scl.get() == nullptr
                ? defaultScl
                : scl.get()
        };
    }
};

}    // namespace farmhub::peripherals::environment
