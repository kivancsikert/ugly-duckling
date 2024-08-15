#pragma once

#include <Arduino.h>

#include <kernel/I2CManager.hpp>
#include <kernel/Log.hpp>
#include <kernel/drivers/BatteryDriver.hpp>

using namespace farmhub::kernel;

namespace farmhub::kernel::drivers {

class Bq27220Driver : public BatteryDriver {
public:
    Bq27220Driver(I2CManager& i2c, gpio_num_t sda, gpio_num_t scl, const uint8_t address = 0x55)
        : wire(i2c.getWireFor(sda, scl))
        , address(address) {
        Log.info("Initializing BQ27220 driver on SDA %d, SCL %d",
            sda, scl);

        wire.beginTransmission(address);
        if (wire.endTransmission() != 0) {
            // TODO Throw an actual exception?
            Log.error("BQ27220 not found at address 0x%02x", address);
            return;
        }
    }

    float getVoltage() override {
        // Read the control status register at 0x0000
        wire.beginTransmission(address);
        wire.write(0x08);
        wire.endTransmission();
        wire.requestFrom(address, (uint8_t) 2);
        uint16_t result = wire.read() | (wire.read() << 8);
        return result / 1000.0;
    }

private:
    TwoWire& wire;
    const uint8_t address;
};

}    // namespace farmhub::kernel::drivers
