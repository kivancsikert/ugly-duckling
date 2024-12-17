#pragma once

#include <exception>
#include <memory>

#include <i2cdev.h>

#include <kernel/Pin.hpp>
#include <kernel/Strings.hpp>

namespace farmhub::kernel {

using std::shared_ptr;

using farmhub::kernel::PinPtr;

using GpioPair = std::pair<PinPtr, PinPtr>;

struct I2CConfig {
public:
    uint8_t address;
    InternalPinPtr sda;
    InternalPinPtr scl;

    std::string toString() const {
        return "I2C address: 0x" + toHexString(address) + ", SDA: " + sda->getName() + ", SCL: " + scl->getName();
    }
};

class I2CBus {
public:
    const i2c_port_t port;
    const InternalPinPtr sda;
    const InternalPinPtr scl;
};

class I2CDevice {
public:
    I2CDevice(const std::string& name, shared_ptr<I2CBus> bus, uint8_t address)
        : name(name)
        , bus(bus)
        , address(address)
        , device({
              .port = bus->port,
              .cfg = {
                  .sda_io_num = bus->sda->getGpio(),
                  .scl_io_num = bus->scl->getGpio(),
                  // TODO Allow this to be configred
                  .sda_pullup_en = false,
                  .scl_pullup_en = false,
                  .master {
                      // TODO Allow clock speed to be configured
                      .clk_speed = 400000,
                  },
              },
              .addr = address,
          }) {
        // TODO Do we need a mutex here?
        i2c_dev_create_mutex(&device);
    }

    ~I2CDevice() {
        i2c_dev_delete_mutex(&device);
    }

    esp_err_t probeRead() {
        return i2c_dev_probe(&device, I2C_DEV_READ);
    }

    uint8_t readRegByte(uint8_t reg) {
        uint8_t value;
        ESP_ERROR_CHECK(i2c_dev_read(&device, &reg, 1, &value, 1));
        return value;
    }

    uint16_t readRegWord(uint8_t reg) {
        uint16_t value;
        ESP_ERROR_CHECK(i2c_dev_read(&device, &reg, 1, &value, 2));
        return value;
    }

    void readReg(uint8_t reg, uint8_t* buffer, size_t length) {
        ESP_ERROR_CHECK(i2c_dev_read(&device, &reg, 1, buffer, length));
    }

    void writeRegByte(uint8_t reg, uint8_t value) {
        ESP_ERROR_CHECK(i2c_dev_write(&device, &reg, 1, &value, 1));
    }

    void writeRegWord(uint8_t reg, uint16_t value) {
        ESP_ERROR_CHECK(i2c_dev_write(&device, &reg, 1, &value, 2));
    }

    void writeReg(uint8_t reg, uint8_t* buffer, size_t length) {
        ESP_ERROR_CHECK(i2c_dev_write(&device, &reg, 1, buffer, length));
    }

private:
    const std::string name;
    const shared_ptr<I2CBus> bus;
    const uint8_t address;
    i2c_dev_t device;
};

class I2CManager {
public:
    I2CManager() {
        ESP_ERROR_CHECK(i2cdev_init());
        buses.reserve(BUS_COUNT);
    }

    ~I2CManager() {
        ESP_ERROR_CHECK(i2cdev_done());
    }

    shared_ptr<I2CDevice> createDevice(const std::string& name, const I2CConfig& config) {
        return createDevice(name, config.sda, config.scl, config.address);
    }

    shared_ptr<I2CDevice> createDevice(const std::string& name, InternalPinPtr sda, InternalPinPtr scl, uint8_t address) {
        auto device = std::make_shared<I2CDevice>(name, getBusFor(sda, scl), address);
        LOGI("Created I2C device %s at address 0x%02x",
            name.c_str(), address);
        // Test if communication is possible
        // esp_err_t err = device->probeRead();
        // if (err != ESP_OK) {
        //     throw std::runtime_error(
        //         "Failed to communicate with I2C device " + name + " at address 0x" + std::to_string(address) + ": " + esp_err_to_name(err);
        // }
        return device;
    }

    shared_ptr<I2CBus> getBusFor(const I2CConfig& config) {
        return getBusFor(config.sda, config.scl);
    }

    shared_ptr<I2CBus> getBusFor(InternalPinPtr sda, InternalPinPtr scl) {
        Lock lock(mutex);
        for (auto bus : buses) {
            if (bus->sda == sda && bus->scl == scl) {
                LOGV("Reusing already registered I2C bus for SDA: %s, SCL: %s",
                    sda->getName().c_str(), scl->getName().c_str());
                return bus;
            }
        }
        if (buses.size() < BUS_COUNT) {
            LOGD("Creating new I2C bus for SDA: %s, SCL: %s",
                sda->getName().c_str(), scl->getName().c_str());
            auto bus = std::make_shared<I2CBus>(I2CBus { .port = static_cast<i2c_port_t>(buses.size()), .sda = sda, .scl = scl });
            buses.push_back(bus);
            return bus;
        }

        throw std::runtime_error("Maximum number of I2C buses reached");
    }

private:
    static constexpr size_t BUS_COUNT = 2;

    Mutex mutex;
    std::vector<shared_ptr<I2CBus>> buses;
};

}    // namespace farmhub::kernel
