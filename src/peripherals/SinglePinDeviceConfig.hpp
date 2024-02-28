#pragma once

#include <chrono>

#include <Arduino.h>

#include <kernel/Configuration.hpp>

using namespace std::chrono;
using namespace farmhub::kernel;

namespace farmhub::peripherals {

class SinglePinDeviceConfig
    : public ConfigurationSection {
public:
    Property<gpio_num_t> pin { this, "pin", GPIO_NUM_NC };
};

}    // namespace farmhub::peripherals::environment
