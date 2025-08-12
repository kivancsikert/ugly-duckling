#pragma once

#include <limits>
#include <utility>

#include <si7021.h>

#include <I2CManager.hpp>

#include <peripherals/I2CSettings.hpp>
#include <peripherals/Peripheral.hpp>

#include "Environment.hpp"

using namespace farmhub::kernel;
using namespace farmhub::peripherals;

namespace farmhub::peripherals::environment {

/**
 * @brief Works with SHT2x or HTU2x.
 */
class Sht2xSensor final
    : public EnvironmentSensor {
public:
    Sht2xSensor(
        const std::string& sensorType,
        const std::shared_ptr<I2CManager>& i2c,
        const I2CConfig& config)
        : bus(i2c->getBusFor(config)) {

        // TODO Add commands to soft/hard reset the sensor
        // TODO Add configuration for fast / slow measurement
        // TODO Add a separate task to do measurements to unblock telemetry collection?

        LOGI("Initializing %s environment sensor with %s",
            sensorType.c_str(), config.toString().c_str());

        ESP_ERROR_THROW(si7021_init_desc(&sensor, bus->port, bus->sda->getGpio(), bus->scl->getGpio()));
    }

    double getTemperature() override {
        float value;
        esp_err_t res = si7021_measure_temperature(&sensor, &value);
        if (res != ESP_OK) {
            LOGD("Could not measure temperature: %s", esp_err_to_name(res));
            return std::numeric_limits<double>::quiet_NaN();
        }
        return value;
    }

    double getMoisture() override {
        float value;
        esp_err_t res = si7021_measure_humidity(&sensor, &value);
        if (res != ESP_OK) {
            LOGD("Could not measure humidity: %s", esp_err_to_name(res));
            return std::numeric_limits<double>::quiet_NaN();
        }
        return value;
    }

private:
    std::shared_ptr<I2CBus> bus;
    i2c_dev_t sensor {};
};

// Type-erased factories for SHT2x and HTU2x environment sensors
inline TypeErasedPeripheralFactory makeFactoryForSht2x(const std::string& sensorKey) {
    // sensorKey: "sht2x" or "htu2x"
    return makePeripheralFactory<I2CSettings>(
        "environment:" + sensorKey,
        "environment",
        [sensorKey](PeripheralInitParameters& params, const std::shared_ptr<I2CSettings>& settings) {
            // Address is fixed to 0x40 for these devices
            I2CConfig i2cConfig = settings->parse(0x40);
            auto sensor = std::make_shared<Sht2xSensor>(sensorKey, params.services.i2c, i2cConfig);
            params.registerFeature("temperature", [sensor](JsonObject& telemetryJson) {
                telemetryJson["value"] = sensor->getTemperature();
            });
            params.registerFeature("moisture", [sensor](JsonObject& telemetryJson) {
                telemetryJson["value"] = sensor->getMoisture();
            });
            return sensor;
        });
}

}    // namespace farmhub::peripherals::environment
