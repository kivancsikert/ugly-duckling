#pragma once

#include <exception>
#include <map>
#include <utility>

#include <Arduino.h>
#include <Wire.h>

#include <devices/Pin.hpp>
#include <kernel/Log.hpp>

namespace farmhub::kernel {

using farmhub::devices::PinPtr;

using GpioPair = std::pair<PinPtr, PinPtr>;
using TwoWireMap = std::map<GpioPair, TwoWire*>;

struct I2CConfig {
public:
    uint8_t address;
    InternalPinPtr sda;
    InternalPinPtr scl;

    String toString() const {
        return String("I2C address: 0x") + String(address, HEX) + ", SDA: " + sda->getName() + ", SCL: " + scl->getName();
    }
};

class I2CManager {
public:
    TwoWire& getWireFor(const I2CConfig& config) {
        return getWireFor(config.sda, config.scl);
    }

    TwoWire& getWireFor(InternalPinPtr sda, InternalPinPtr scl) {
        GpioPair key = std::make_pair(sda, scl);
        auto it = wireMap.find(key);
        if (it != wireMap.end()) {
            Log.trace("Reusing already registered I2C bus for SDA: %s, SCL: %s",
                sda->getName().c_str(), scl->getName().c_str());
            return *(it->second);
        } else {
            Log.trace("Creating new I2C bus for SDA: %s, SCL: %s",
                sda->getName().c_str(), scl->getName().c_str());
            if (nextBus >= 2) {
                throw std::runtime_error("Maximum number of I2C buses reached");
            }
            TwoWire* wire = new TwoWire(nextBus++);
            if (!wire->begin(sda->getGpio(), scl->getGpio())) {
                throw std::runtime_error(
                    String("Failed to initialize I2C bus for SDA: " + sda->getName() + ", SCL: " + scl->getName()).c_str());
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
