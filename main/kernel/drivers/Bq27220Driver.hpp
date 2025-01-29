#pragma once

#include <kernel/I2CManager.hpp>
#include <kernel/drivers/BatteryDriver.hpp>

using namespace farmhub::kernel;

namespace farmhub::kernel::drivers {

class Bq27220Driver : public BatteryDriver {
public:
    Bq27220Driver(std::shared_ptr<I2CManager> i2c, InternalPinPtr sda, InternalPinPtr scl, const uint8_t address = 0x55)
        : device(i2c->createDevice("battery:bq27220", sda, scl, address)) {
        LOGI("Initializing BQ27220 driver on SDA %s, SCL %s",
            sda->getName().c_str(), scl->getName().c_str());

        auto deviceType = readControlWord(0x0001);
        if (deviceType != 0x0220) {
            LOGE("BQ27220 at address 0x%02x is not a BQ27220 (0x%04x)",
                address, deviceType);
            return;
        }

        LOGI("Found BQ27220 at address 0x%02x, FW version 0x%04x, HW version 0x%04x",
            address, readControlWord(0x0002), readControlWord(0x0003));
    }

    float getVoltage() override {
        // LOGV("Capacityt: %d/%d", readRegWord(0x10), readRegWord(0x12));
        return device->readRegWord(0x08) / 1000.0;
    }

    float getCurrent() {
        return readSigned(0x0C) / 1.0;
    }

    float getTemperature() {
        return device->readRegWord(0x06) * 0.1 - 273.2;
    }

// protected:
//     void populateTelemetry(JsonObject& json) override {
//         BatteryDriver::populateTelemetry(json);
//         json["current"] = getCurrent();
//         auto status = device->readRegWord(0x0A);
//         json["status"] = status;
//         json["charging"] = (status & 0x0001) == 0;
//         json["temperature"] = getTemperature();
//     }

private:
    int16_t readSigned(uint8_t reg) {
        return static_cast<int16_t>(device->readRegWord(reg));
    }

    uint16_t readControlWord(uint16_t subcommand) {
        device->writeRegWord(0x00, subcommand);
        return device->readRegByte(0x40) | (device->readRegByte(0x41) << 8);
    }

    shared_ptr<I2CDevice> device;
};

}    // namespace farmhub::kernel::drivers
