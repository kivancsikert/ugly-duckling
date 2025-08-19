#pragma once

#include <I2CManager.hpp>
#include <drivers/BatteryDriver.hpp>

using namespace farmhub::kernel;

namespace farmhub::kernel::drivers {

class Bq27220Driver final : public BatteryDriver {
public:
    Bq27220Driver(
        const std::shared_ptr<I2CManager>& i2c,
        const InternalPinPtr& sda,
        const InternalPinPtr& scl,
        const BatteryParameters& parameters)
        : Bq27220Driver(i2c, sda, scl, 0x55, parameters) {
    }

    Bq27220Driver(
        const std::shared_ptr<I2CManager>& i2c,
        const InternalPinPtr& sda,
        const InternalPinPtr& scl,
        uint8_t address,
        const BatteryParameters& parameters)
        : BatteryDriver(parameters)
        , device(i2c->createDevice("battery:bq27220", sda, scl, address)) {
        LOGI("Initializing BQ27220 driver on SDA %s, SCL %s, address 0x%02X",
            sda->getName().c_str(), scl->getName().c_str(), address);

        auto deviceType = readControlWord(0x0001);
        if (deviceType != 0x0220) {
            LOGE("BQ27220 at address 0x%02x is not a BQ27220 (0x%04x)",
                address, deviceType);
            return;
        }

        LOGI("Found BQ27220 at address 0x%02x, FW version 0x%04x, HW version 0x%04x",
            address, readControlWord(0x0002), readControlWord(0x0003));
    }

    int getVoltage() override {
        // LOGV("Capacityt: %d/%d", readRegWord(0x10), readRegWord(0x12));
        return device->readRegWord(0x08);
    }

    double getCurrent() {
        return readSigned(0x0C) / 1.0;
    }

    double getTemperature() {
        return (device->readRegWord(0x06) * 0.1) - 273.2;
    }

    // protected:
    //     void populateTelemetry(JsonObject& json) {
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

    std::shared_ptr<I2CDevice> device;
};

}    // namespace farmhub::kernel::drivers
