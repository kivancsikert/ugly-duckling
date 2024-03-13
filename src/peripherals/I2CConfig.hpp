#pragma once

#include <chrono>

#include <Arduino.h>

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
    Property<String> address { this, "address" };
    Property<gpio_num_t> sda { this, "sda", GPIO_NUM_NC };
    Property<gpio_num_t> scl { this, "scl", GPIO_NUM_NC };

    I2CConfig parse() const {
        return parse(-1, GPIO_NUM_NC, GPIO_NUM_NC);
    }

    I2CConfig parse(uint8_t defaultAddress, gpio_num_t defaultSda, gpio_num_t defaultScl) const {
        return {
            address.get().isEmpty()
                ? defaultAddress
                : (uint8_t) strtol(address.get().c_str(), nullptr, 0),
            sda.get() == GPIO_NUM_NC
                ? defaultSda
                : sda.get(),
            scl.get() == GPIO_NUM_NC
                ? defaultScl
                : scl.get()
        };
    }
};

}    // namespace farmhub::peripherals::environment
