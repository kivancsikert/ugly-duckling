#pragma once

#include <chrono>
#include <memory>
#include <optional>
#include <string>

#include <bq27220.h>

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

        // Check if we can communicate with the device and initialize bus
        ESP_ERROR_THROW(device->probeRead());

        // Get the bus handle
        auto* bus = device->getBus()->lookupHandle();

        // Initialize BQ27220 on existing bus
        // TODO Synchronize speed with other devices on the same bus?
        ESP_ERROR_THROW(bq27220_init(bus, device->getAddress(), 400000, &gauge));
    }

    int getVoltage() override {
        int value;
        ESP_ERROR_THROW(bq27220_read_voltage_mv(gauge, &value));
        return value;
    }

    double getPercentage() override {
        int value;
        ESP_ERROR_THROW(bq27220_read_state_of_charge_percent(gauge, &value));
        return value;
    }

    std::optional<double> getCurrent() override {
        int value;
        ESP_ERROR_THROW(bq27220_read_average_current_ma(gauge, &value));
        return value;
    }

    double getTemperature() {
        float value;
        ESP_ERROR_THROW(bq27220_read_temperature_c(gauge, &value));
        return value;
    }

    std::optional<seconds> getTimeToEmpty() override {
        int value;
        esp_err_t err = bq27220_read_time_to_empty_min(gauge, &value);
        switch (err) {
            case ESP_OK:
                return minutes(value);
            case ESP_ERR_INVALID_RESPONSE:
                // Not discharging
                return std::nullopt;
            default:
                throw EspException(err);
        }
    }

    std::optional<seconds> getTimeToFull() {
        int value;
        esp_err_t err = bq27220_read_time_to_full_min(gauge, &value);
        switch (err) {
            case ESP_OK:
                return minutes(value);
            case ESP_ERR_INVALID_RESPONSE:
                // Not charging
                return std::nullopt;
            default:
                throw EspException(err);
        }
    }

private:
    std::shared_ptr<I2CDevice> device;
    bq27220_handle_t gauge = nullptr;
};

}    // namespace farmhub::kernel::drivers
