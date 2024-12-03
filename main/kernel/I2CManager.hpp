#pragma once

#include <exception>
#include <map>
#include <memory>
#include <utility>

#include <Arduino.h>
#include <Wire.h>

#include <kernel/Log.hpp>
#include <kernel/Pin.hpp>

namespace farmhub::kernel {

using std::shared_ptr;

using farmhub::kernel::PinPtr;

using GpioPair = std::pair<PinPtr, PinPtr>;

struct I2CConfig {
public:
    uint8_t address;
    InternalPinPtr sda;
    InternalPinPtr scl;

    String toString() const {
        return String("I2C address: 0x") + String(address, HEX) + ", SDA: " + sda->getName() + ", SCL: " + scl->getName();
    }
};

class I2CBus {
public:
    I2CBus(TwoWire& wire)
        : wire(wire) {
    }

    TwoWire& wire;
    Mutex mutex;
};

class I2CTransmission;

class I2CDevice {
public:
    I2CDevice(const String& name, I2CBus& bus, uint8_t address)
        : name(name)
        , bus(bus)
        , address(address) {
    }

private:
    const String name;
    I2CBus& bus;
    const uint8_t address;

    friend class I2CTransmission;
};

class I2CTransmission {
public:
    I2CTransmission(shared_ptr<I2CDevice> device)
        : device(device)
        , lock(device->bus.mutex) {
        wire().beginTransmission(device->address);
    }

    ~I2CTransmission() {
        auto result = wire().endTransmission();
        if (result != 0) {
            LOGE("Communication unsuccessful with I2C device %s at address 0x%02x, result: %d",
                device->name.c_str(), device->address, result);
        }
    }

    size_t requestFrom(size_t len, bool stopBit = true) {
        LOGV("Requesting %d bytes from I2C device %s at address 0x%02x",
            len, device->name.c_str(), device->address);
        auto count = wire().requestFrom(device->address, len, stopBit);
        LOGV("Received %d bytes from I2C device %s at address 0x%02x",
            count, device->name.c_str(), device->address);
        return count;
    }

    size_t write(uint8_t data) {
        LOGV("Writing 0x%02x to I2C device %s at address 0x%02x",
            data, device->name.c_str(), device->address);
        auto count = wire().write(data);
        LOGV("Wrote %d bytes to I2C device %s at address 0x%02x",
            count, device->name.c_str(), device->address);
        return count;
    }

    size_t write(const uint8_t* data, size_t quantity) {
        LOGV("Writing %d bytes to I2C device %s at address 0x%02x",
            quantity, device->name.c_str(), device->address);
        auto count = wire().write(data, quantity);
        LOGV("Wrote %d bytes to I2C device %s at address 0x%02x",
            count, device->name.c_str(), device->address);
        return count;
    }

    int available() {
        return wire().available();
    }

    int read() {
        auto value = wire().read();
        LOGV("Read 0x%02x from I2C device %s at address 0x%02x",
            value, device->name.c_str(), device->address);
        return value;
    }

    int peek() {
        auto value = wire().peek();
        LOGV("Peeked 0x%02x from I2C device %s at address 0x%02x",
            value, device->name.c_str(), device->address);
        return value;
    }

    void flush() {
        LOGV("Flushing I2C device %s at address 0x%02x",
            device->name.c_str(), device->address);
        wire().flush();
    }

private:
    inline TwoWire& wire() const {
        return device->bus.wire;
    }

    shared_ptr<I2CDevice> device;
    Lock lock;
};

class I2CManager {
public:
    TwoWire& getWireFor(const I2CConfig& config) {
        return getWireFor(config.sda, config.scl);
    }

    TwoWire& getWireFor(InternalPinPtr sda, InternalPinPtr scl) {
        return getBusFor(sda, scl).wire;
    }

    shared_ptr<I2CDevice> createDevice(const String& name, const I2CConfig& config) {
        return createDevice(name, config.sda, config.scl, config.address);
    }

    shared_ptr<I2CDevice> createDevice(const String& name, InternalPinPtr sda, InternalPinPtr scl, uint8_t address) {
        auto device = std::make_shared<I2CDevice>(name, getBusFor(sda, scl), address);
        LOGI("Created I2C device %s at address 0x%02x",
            name.c_str(), address);
        // Test if communication is possible
        I2CTransmission tx(device);
        return device;
    }

private:
    I2CBus& getBusFor(InternalPinPtr sda, InternalPinPtr scl) {
        GpioPair key = std::make_pair(sda, scl);
        auto it = busMap.find(key);
        if (it != busMap.end()) {
            LOGV("Reusing already registered I2C bus for SDA: %s, SCL: %s",
                sda->getName().c_str(), scl->getName().c_str());
            return *(it->second);
        } else {
            LOGD("Creating new I2C bus for SDA: %s, SCL: %s",
                sda->getName().c_str(), scl->getName().c_str());
            if (nextBus >= 2) {
                throw std::runtime_error("Maximum number of I2C buses reached");
            }
            TwoWire* wire = new TwoWire(nextBus++);
            if (!wire->begin(sda->getGpio(), scl->getGpio())) {
                throw std::runtime_error(
                    String("Failed to initialize I2C bus for SDA: " + sda->getName() + ", SCL: " + scl->getName()).c_str());
            }
            I2CBus* bus = new I2CBus(*wire);
            busMap[key] = bus;
            return *bus;
        }
    }

    uint8_t nextBus = 0;

    std::map<GpioPair, I2CBus*> busMap;
};

}    // namespace farmhub::kernel
