#pragma once

#include <Arduino.h>

#include <kernel/I2CManager.hpp>
#include <kernel/Log.hpp>
#include <kernel/drivers/BatteryDriver.hpp>

using namespace farmhub::kernel;

namespace farmhub::kernel::drivers {

class Bq27220Driver : public BatteryDriver {
public:
    Bq27220Driver(I2CManager& i2cManager, gpio_num_t sda, gpio_num_t scl, const uint8_t address = 0x55)
        : i2c(i2cManager.getAccessorFor(sda, scl, address))
        , address(address) {
        Log.info("Initializing BQ27220 driver on SDA %d, SCL %d",
            sda, scl);

        if (!i2c.begin()) {
            // TODO Throw an actual exception?
            Log.error("BQ27220 not found at address 0x%02x", address);
            return;
        }

        auto deviceType = readControlWord(0x0001);
        if (deviceType != 0x0220) {
            Log.error("BQ27220 at address 0x%02x is not a BQ27220 (0x%04x)",
                address, deviceType);
            return;
        }

        Log.info("Found BQ27220 at address 0x%02x, FW version 0x%04x, HW version 0x%04x",
            address, readControlWord(0x0002), readControlWord(0x0003));
    }

    float getVoltage() override {
        // Log.trace("Capacityt: %d/%d", readWord(0x10), readWord(0x12));
        return i2c.readWord(0x08) / 1000.0;
    }

    float getCurrent() {
        return i2c.readSignedWord(0x0C) / 1.0;
    }

    float getTemperature() {
        return i2c.readWord(0x06) * 0.1 - 273.2;
    }

protected:
    void populateTelemetry(JsonObject& json) override {
        BatteryDriver::populateTelemetry(json);
        json["current"] = getCurrent();
        auto status = i2c.readWord(0x0A);
        json["status"] = status;
        json["charging"] = (status & 0x0001) == 0;
        json["temperature"] = getTemperature();
    }

private:
    uint16_t readControlWord(uint16_t subcommand) {
        i2c.writeWord(0x00, subcommand);
        return i2c.readByte(0x40) | (i2c.readByte(0x41) << 8);
    }

    I2CAccessor i2c;
    const uint8_t address;
};

}    // namespace farmhub::kernel::drivers
