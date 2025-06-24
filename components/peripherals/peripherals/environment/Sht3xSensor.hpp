#pragma once

#include <limits>
#include <utility>

#include <sht3x.h>

#include <BootClock.hpp>
#include <Concurrent.hpp>
#include <I2CManager.hpp>

#include <peripherals/I2CSettings.hpp>
#include <peripherals/Peripheral.hpp>

#include "Environment.hpp"

using namespace farmhub::kernel;
using namespace farmhub::peripherals;

namespace farmhub::peripherals::environment {

class Sht3xSensor final
    : public EnvironmentSensor {
public:
    Sht3xSensor(
        const std::string& sensorType,
        const std::shared_ptr<I2CManager>& i2c,
        const I2CConfig& config)
        : bus(i2c->getBusFor(config)) {

        // TODO Add commands to soft/hard reset the sensor
        // TODO Add configuration for fast / slow measurement
        // TODO Add a separate task to do measurements to unblock telemetry collection?

        LOGI("Initializing %s environment sensor with %s",
            sensorType.c_str(), config.toString().c_str());

        ESP_ERROR_THROW(sht3x_init_desc(&sensor, config.address, bus->port, bus->sda->getGpio(), bus->scl->getGpio()));
        ESP_ERROR_THROW(sht3x_init(&sensor));
    }

    double getTemperature() override {
        Lock lock(mutex);
        updateMeasurement();
        return temperature;
    }

    double getHumidity() override {
        Lock lock(mutex);
        updateMeasurement();
        return humidity;
    }

private:
    void updateMeasurement() {
        Lock lock(mutex);
        auto now = boot_clock::now();
        if (now - this->lastMeasurementTime < 1s) {
            // Do not measure more often than once per second
            return;
        }
        float fTemp;
        float fHumidity;
        esp_err_t res = sht3x_measure(&sensor, &fTemp, &fHumidity);
        if (res == ESP_OK) {
            LOGV("Measured temperature: %.2f °C, humidity: %.2f %%",
                fTemp, fHumidity);
            temperature = fTemp;
            humidity = fHumidity;
        } else {
            LOGD("Could not measure temperature: %s", esp_err_to_name(res));
            temperature = std::numeric_limits<double>::quiet_NaN();
            humidity = std::numeric_limits<double>::quiet_NaN();
        }
        this->lastMeasurementTime = now;
    }

    std::shared_ptr<I2CBus> bus;
    sht3x_t sensor {};

    Mutex mutex;
    std::chrono::time_point<boot_clock> lastMeasurementTime;
    double temperature = std::numeric_limits<double>::quiet_NaN();
    double humidity = std::numeric_limits<double>::quiet_NaN();
};

}    // namespace farmhub::peripherals::environment
