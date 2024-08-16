#pragma once

#include <Arduino.h>

#include <INA219.h>

#include <kernel/I2CManager.hpp>
#include <kernel/Log.hpp>
#include <kernel/drivers/CurrentSenseDriver.hpp>

using namespace farmhub::kernel;

namespace farmhub::kernel::drivers {

class Ina219Driver : public CurrentSenseDriver {
public:
    Ina219Driver(I2CManager& i2cManager, gpio_num_t sda, gpio_num_t scl, uint8_t address = 0x40)
        : sensor(address, &i2cManager.getWireFor(sda, scl)) {

        Log.info("Initializing INA219 driver on SDA %d, SCL %d, address: 0x%02x",
            sda, scl, address);

        if (!sensor.begin()) {
            Log.error("INA219 not found at address 0x%02x", address);
            return;
        }
        Log.trace("Found INA219 at address 0x%02x", address);
    }

    double readCurrent() override {
        return sensor.getCurrent_mA() / 1000.0;
    }

private:
    INA219 sensor;
};

}    // namespace farmhub::kernel::drivers
