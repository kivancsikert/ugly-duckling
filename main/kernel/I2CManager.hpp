#pragma once

#include <exception>
#include <map>
#include <memory>
#include <utility>

#include <Arduino.h>
#include <Wire.h>

#include <devices/Pin.hpp>
#include <kernel/Log.hpp>

namespace farmhub::kernel {

using std::shared_ptr;

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

class I2CTransmission;

class I2CDevice {
public:
    I2CDevice(const String& name, TwoWire& wire, uint8_t address)
        : name(name)
        , wire(wire)
        , address(address) {
    }

private:
    const String name;
    TwoWire& wire;
    const uint8_t address;

    friend class I2CTransmission;
};

class I2CTransmission {
public:
    I2CTransmission(shared_ptr<I2CDevice> device)
        : device(device) {
        device->wire.beginTransmission(device->address);
    }

    ~I2CTransmission() {
        auto result = device->wire.endTransmission();
        if (result != 0) {
            Log.error("Communication unsuccessful with I2C device %s at address 0x%02x, result: %d",
                device->name.c_str(), device->address, result);
        }
    }

    size_t requestFrom(size_t len, bool stopBit = true) {
        Log.trace("Requesting %d bytes from I2C device %s at address 0x%02x",
            len, device->name.c_str(), device->address);
        auto count = device->wire.requestFrom(device->address, len, stopBit);
        Log.trace("Received %d bytes from I2C device %s at address 0x%02x",
            count, device->name.c_str(), device->address);
        return count;
    }

    size_t write(uint8_t data) {
        Log.trace("Writing 0x%02x to I2C device %s at address 0x%02x",
            data, device->name.c_str(), device->address);
        auto count = device->wire.write(data);
        Log.trace("Wrote %d bytes to I2C device %s at address 0x%02x",
            count, device->name.c_str(), device->address);
        return count;
    }

    size_t write(const uint8_t *data, size_t quantity) {
        Log.trace("Writing %d bytes to I2C device %s at address 0x%02x",
            quantity, device->name.c_str(), device->address);
        auto count = device->wire.write(data, quantity);
        Log.trace("Wrote %d bytes to I2C device %s at address 0x%02x",
            count, device->name.c_str(), device->address);
        return count;
    }

    int available() {
        return device->wire.available();
    }

    int read() {
        auto value = device->wire.read();
        Log.trace("Read 0x%02x from I2C device %s at address 0x%02x",
            value, device->name.c_str(), device->address);
        return value;
    }

    int peek() {
        auto value = device->wire.peek();
        Log.trace("Peeked 0x%02x from I2C device %s at address 0x%02x",
            value, device->name.c_str(), device->address);
        return value;
    }

    void flush() {
        Log.trace("Flushing I2C device %s at address 0x%02x",
            device->name.c_str(), device->address);
        device->wire.flush();
    }

private:
    shared_ptr<I2CDevice> device;
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
            Log.debug("Creating new I2C bus for SDA: %s, SCL: %s",
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

    shared_ptr<I2CDevice> createDevice(const String& name, const I2CConfig& config) {
        return createDevice(name, config.sda, config.scl, config.address);
    }

    shared_ptr<I2CDevice> createDevice(const String& name, InternalPinPtr sda, InternalPinPtr scl, uint8_t address) {
        auto device = std::make_shared<I2CDevice>(name, getWireFor(sda, scl), address);
        Log.info("Created I2C device %s at address 0x%02x",
            name.c_str(), address);
        // Test if communication is possible
        I2CTransmission tx(device);
        return device;
    }

private:
    uint8_t nextBus = 0;

    TwoWireMap wireMap;
};

}    // namespace farmhub::kernel
