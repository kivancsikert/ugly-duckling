#pragma once

#include <Arduino.h>

#include <kernel/Log.hpp>
#include <kernel/drivers/BatteryDriver.hpp>

using namespace farmhub::kernel;

namespace farmhub::kernel::drivers {

class Ina219Driver : public BatteryDriver {
public:
    Ina219Driver(gpio_num_t sda, gpio_num_t scl) {
        Log.info("Initializing INA219 driver on SDA %d, SCL %d", sda, scl);
    }

    float getVoltage() {
        return 0.0;
    }

    float getCurrent() {
        return 0.0;
    }

    float getPower() {
        return 0.0;
    }
};

}    // namespace farmhub::kernel::drivers
