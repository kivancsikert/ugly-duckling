#pragma once

#include <map>
#include <utility>
#include <exception>

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

class I2CManager {
public:
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
