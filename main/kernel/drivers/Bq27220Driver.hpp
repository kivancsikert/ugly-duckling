#pragma once

#include <Arduino.h>

#include <kernel/I2CManager.hpp>
#include <kernel/Log.hpp>
#include <kernel/drivers/BatteryDriver.hpp>

using namespace farmhub::kernel;

namespace farmhub::kernel::drivers {

class Bq27220Driver : public BatteryDriver {
public:
    Bq27220Driver(I2CManager& i2c, InternalPinPtr sda, InternalPinPtr scl, const uint8_t address = 0x55)
        : device(i2c.createDevice("battery:bq27220", sda, scl, address)) {
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
        // LOGV("Capacityt: %d/%d", readWord(0x10), readWord(0x12));
        return readWord(0x08) / 1000.0;
    }

    float getCurrent() {
        return readSigned(0x0C) / 1.0;
    }

    float getTemperature() {
        return readWord(0x06) * 0.1 - 273.2;
    }

protected:
    void populateTelemetry(JsonObject& json) override {
        BatteryDriver::populateTelemetry(json);
        json["current"] = getCurrent();
        auto status = readWord(0x0A);
        json["status"] = status;
        json["charging"] = (status & 0x0001) == 0;
        json["temperature"] = getTemperature();
    }

private:
    bool readFrom(uint8_t reg, uint8_t* buffer, size_t length) {
        {
            I2CTransmission tx(device);
            tx.write(reg);
        }
        {
            I2CTransmission tx(device);
            auto rxResult = tx.requestFrom((uint8_t) length);
            if (rxResult != length) {
                LOGE("Failed to read from 0x%02x: %d", reg, rxResult);
                return false;
            }
            for (size_t i = 0; i < length; i++) {
                buffer[i] = tx.read();
            }
        }
        return true;
    }

    void writeTo(uint8_t reg, const uint8_t* buffer, size_t length) {
        I2CTransmission tx(device);
        tx.write(reg);
        tx.write(buffer, length);
    }

    uint8_t readByte(uint8_t reg) {
        uint8_t buffer;
        readFrom(reg, &buffer, 1);
        return buffer;
    }

    uint16_t readWord(uint8_t reg) {
        uint16_t buffer;
        readFrom(reg, reinterpret_cast<uint8_t*>(&buffer), 2);
        return buffer;
    }

    int16_t readSigned(uint8_t reg) {
        return static_cast<int16_t>(readWord(reg));
    }

    void writeWord(uint8_t reg, uint16_t value) {
        uint16_t buffer = value;
        writeTo(reg, reinterpret_cast<uint8_t*>(&buffer), 2);
    }

    uint16_t readControlWord(uint16_t subcommand) {
        writeWord(0x00, subcommand);
        return readByte(0x40) | (readByte(0x41) << 8);
    }

    shared_ptr<I2CDevice> device;
};

}    // namespace farmhub::kernel::drivers
