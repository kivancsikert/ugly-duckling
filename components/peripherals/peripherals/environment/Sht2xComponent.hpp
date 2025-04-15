#pragma once

#include <limits>

#include <si7021.h>

#include <Component.hpp>
#include <I2CManager.hpp>
#include <Telemetry.hpp>

#include <peripherals/I2CConfig.hpp>
#include <peripherals/Peripheral.hpp>

using namespace farmhub::kernel;
using namespace farmhub::peripherals;

namespace farmhub::peripherals::environment {

/**
 * @brief Works with SHT2x or HTU2x.
 */
class Sht2xComponent
    : public Component,
      public TelemetryProvider {
public:
    Sht2xComponent(
        const std::string& name,
        const std::string& sensorType,
        std::shared_ptr<MqttRoot> mqttRoot,
        std::shared_ptr<I2CManager> i2c,
        const I2CConfig& config)
        : Component(name, mqttRoot)
        , bus(i2c->getBusFor(config)) {

        // TODO Add commands to soft/hard reset the sensor
        // TODO Add configuration for fast / slow measurement
        // TODO Add a separate task to do measurements to unblock telemetry collection?

        LOGI("Initializing %s environment sensor with %s",
            sensorType.c_str(), config.toString().c_str());

        ESP_ERROR_THROW(si7021_init_desc(&sensor, bus->port, bus->sda->getGpio(), bus->scl->getGpio()));
    }

    void populateTelemetry(JsonObject& json) override {
        json["temperature"] = getTemperature();
        json["humidity"] = getHumidity();
    }

private:
    float getTemperature() {
        float value;
        esp_err_t res = si7021_measure_temperature(&sensor, &value);
        if (res != ESP_OK) {
            LOGD("Could not measure temperature: %s", esp_err_to_name(res));
            return std::numeric_limits<float>::quiet_NaN();
        } else {
            return value;
        }
    }

    float getHumidity() {
        float value;
        esp_err_t res = si7021_measure_humidity(&sensor, &value);
        if (res != ESP_OK) {
            LOGD("Could not measure humidity: %s", esp_err_to_name(res));
            return std::numeric_limits<float>::quiet_NaN();
        } else {
            return value;
        }
    }

    std::shared_ptr<I2CBus> bus;
    i2c_dev_t sensor {};
};

}    // namespace farmhub::peripherals::environment
