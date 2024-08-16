#pragma once

#include <map>
#include <utility>

#include <Arduino.h>
#include <Wire.h>

#include <kernel/Log.hpp>

namespace farmhub::kernel {

using GpioPair = std::pair<gpio_num_t, gpio_num_t>;
using TwoWireMap = std::map<GpioPair, TwoWire*>;

struct I2CConfig {
public:
    uint8_t address;
    gpio_num_t sda;
    gpio_num_t scl;

    String toString() const {
        return String("I2C address: 0x") + String(address, HEX) + ", SDA: " + String(sda) + ", SCL: " + String(scl);
    }
};

class I2CAccessor {
public:
    I2CAccessor(TwoWire& wire, uint8_t address)
        : wire(wire)
        , address(address) {
    }

    bool begin() {
        wire.beginTransmission(address);
        return wire.endTransmission() == 0;
    }

    bool readFrom(uint8_t reg, uint8_t* buffer, size_t length) {
        wire.beginTransmission(address);
        wire.write(reg);
        auto txResult = wire.endTransmission();
        if (txResult != 0) {
            Log.error("Failed to write to 0x%02x: %d", reg, txResult);
            return false;
        }

        auto rxResult = wire.requestFrom(address, (uint8_t) length);
        if (rxResult != length) {
            Log.error("Failed to read from 0x%02x: %d", reg, rxResult);
            return false;
        }
        for (size_t i = 0; i < length; i++) {
            buffer[i] = wire.read();
            // Log.trace("Read 0x%02x from 0x%02x", buffer[i], reg);
        }
        return true;
    }

    bool writeTo(uint8_t reg, const uint8_t* buffer, size_t length) {
        wire.beginTransmission(address);
        wire.write(reg);
        for (size_t i = 0; i < length; i++) {
            // Log.trace("Writing 0x%02x to 0x%02x", buffer[i], reg);
            wire.write(buffer[i]);
        }
        return wire.endTransmission() == 0;
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

    int16_t readSignedWord(uint8_t reg) {
        return static_cast<int16_t>(readWord(reg));
    }

    bool writeWord(uint8_t reg, uint16_t value) {
        uint16_t buffer = value;
        return writeTo(reg, reinterpret_cast<uint8_t*>(&buffer), 2);
    }

private:
    TwoWire& wire;
    uint8_t address;
};

class I2CManager {
public:
    I2CAccessor getAccessorFor(const I2CConfig& config) {
        return getAccessorFor(config.sda, config.scl, config.address);
    }

    I2CAccessor getAccessorFor(gpio_num_t sda, gpio_num_t scl, uint8_t address) {
        return I2CAccessor(getWireFor(sda, scl), address);
    }

    TwoWire& getWireFor(const I2CConfig& config) {
        return getWireFor(config.sda, config.scl);
    }

    TwoWire& getWireFor(gpio_num_t sda, gpio_num_t scl) {
        GpioPair key = std::make_pair(sda, scl);
        auto it = wireMap.find(key);
        if (it != wireMap.end()) {
            Log.trace("Reusing already registered I2C bus for SDA: %d, SCL: %d", sda, scl);
            return *(it->second);
        } else {
            Log.trace("Creating new I2C bus for SDA: %d, SCL: %d", sda, scl);
            TwoWire* wire = new TwoWire(nextBus++);
            if (!wire->begin(sda, scl)) {
                throw std::runtime_error(
                    String("Failed to initialize I2C bus for SDA: " + String(sda) + ", SCL: " + String(scl)).c_str());
            }
            wireMap[key] = wire;
            return *wire;
        }
    }

private:
    uint8_t nextBus = 0;

    TwoWireMap wireMap;
};

}    // namespace farmhub::kernel
