#pragma once

#include <limits>

#include <sht3x.h>

#include <Component.hpp>
#include <I2CManager.hpp>
#include <Telemetry.hpp>

#include <peripherals/I2CConfig.hpp>
#include <peripherals/Peripheral.hpp>

using namespace farmhub::kernel;
using namespace farmhub::peripherals;

namespace farmhub::peripherals::environment {

class Sht3xComponent
    : public Component,
      public TelemetryProvider {
public:
    Sht3xComponent(
        const std::string& name,
        const std::string& sensorType,
        std::shared_ptr<MqttRoot> mqttRoot,
        std::shared_ptr<I2CManager> i2c,
        I2CConfig config)
        : Component(name, mqttRoot)
        , bus(i2c->getBusFor(config)) {

        // TODO Add commands to soft/hard reset the sensor
        // TODO Add configuration for fast / slow measurement
        // TODO Add a separate task to do measurements to unblock telemetry collection?

        LOGI("Initializing %s environment sensor with %s",
            sensorType.c_str(), config.toString().c_str());

        ESP_ERROR_THROW(sht3x_init_desc(&sensor, config.address, bus->port, bus->sda->getGpio(), bus->scl->getGpio()));
        ESP_ERROR_THROW(sht3x_init(&sensor));
    }

    void populateTelemetry(JsonObject& json) override {
        float temperature;
        float humidity;
        esp_err_t res = sht3x_measure(&sensor, &temperature, &humidity);
        if (res != ESP_OK) {
            LOGD("Could not measure temperature: %s", esp_err_to_name(res));
            temperature = std::numeric_limits<float>::quiet_NaN();
            humidity = std::numeric_limits<float>::quiet_NaN();
        }

        json["temperature"] = temperature;
        json["humidity"] = humidity;
    }

private:
    std::shared_ptr<I2CBus> bus;
    sht3x_t sensor {};
};

}    // namespace farmhub::peripherals::environment
