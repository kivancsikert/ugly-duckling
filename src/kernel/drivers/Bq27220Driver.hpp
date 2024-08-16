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

        if (readControlWord(0x0001) != 0x0220) {
            Log.error("BQ27220 at address 0x%02x is not a BQ27220", address);
            return;
        }

        Log.info("Found BQ27220 at address 0x%02x, FW version %04x, HW version %04x",
            address, readControlWord(0x0002), readControlWord(0x0003));
    }

    float getVoltage() override {
        // Log.trace("Capacityt: %d/%d", readWord(0x10), readWord(0x12));
        return readWord(0x08) / 1000.0;
    }

    float getCurrent() {
        return (float) ((int16_t) readWord(0x0A));
    }

protected:
    void populateTelemetry(JsonObject& json) override {
        BatteryDriver::populateTelemetry(json);
        json["current"] = getCurrent();
    }

private:
    uint8_t readByte(uint8_t reg) {
        wire.beginTransmission(address);
        wire.write(reg);
        wire.endTransmission();
        wire.requestFrom(address, (uint8_t) 2);
        uint8_t result = wire.read();
        // Log.trace("Read 0x%02x from 0x%02x", result, reg);
        return result;
    }

    uint16_t readWord(uint8_t reg) {
        wire.beginTransmission(address);
        wire.write(reg);
        wire.endTransmission();
        wire.requestFrom(address, (uint8_t) 2);
        uint16_t result = wire.read() | (wire.read() << 8);
        // Log.trace("Read 0x%04x from 0x%02x", result, reg);
        return result;
    }

    bool writeWord(uint8_t reg, uint16_t value) {
        // Log.trace("Writing 0x%04x to 0x%02x", value, reg);
        wire.beginTransmission(address);
        wire.write(reg);
        wire.write(value & 0xFF);
        wire.write((value >> 8) & 0xFF);
        return wire.endTransmission() == 0;
    }

    uint16_t readControlWord(uint16_t subcommand) {
        writeWord(0x00, subcommand);
        return readByte(0x40) | (readByte(0x41) << 8);
    }

    TwoWire& wire;
    const uint8_t address;
};

}    // namespace farmhub::kernel::drivers
