#pragma once

#include <memory>
#include <utility>

#include <ds18x20.h>

#include <BootClock.hpp>
#include <Configuration.hpp>

#include <peripherals/Peripheral.hpp>
#include <peripherals/SinglePinSettings.hpp>
#include <peripherals/api/ITemperatureSensor.hpp>

using namespace farmhub::kernel;
using namespace farmhub::kernel::mqtt;
using namespace farmhub::peripherals;

namespace farmhub::peripherals::environment {

/**
 * @brief Support for DS18B20 soil temperature sensor.
 *
 * Note: Needs a 4.7k pull-up resistor between the data and power lines.
 */
class Ds18B20SoilSensor final
    : public ITemperatureSensor,
      public Peripheral {
public:
    explicit Ds18B20SoilSensor(
        const std::string& name,
        const InternalPinPtr& pin)
        : Peripheral(name)
        , pin(pin) {

        LOGI("Initializing DS18B20 soil temperature sensor '%s' on pin %s",
            name.c_str(), pin->getName().c_str());

        // We rely on the external resistor for pull-up
        gpio_set_pull_mode(pin->getGpio(), GPIO_FLOATING);

        LOGV("Locating DS18B20 sensors on bus...");
        size_t sensorCount;
        // TODO How many slots do we need here actually?
        int maxSensors = 1;

        esp_err_t searchResult = ds18x20_scan_devices(pin->getGpio(), &sensor, maxSensors, &sensorCount);
        if (searchResult == ESP_OK) {
            if (sensorCount == 0U) {
                throw PeripheralCreationException("No DS18B20 sensors found on bus");
            }
            LOGD("Found a DS18B20 at address: %016llX", sensor);
        } else {
            throw PeripheralCreationException("Error searching for DS18B20 devices: " + std::string(esp_err_to_name(searchResult)));
        }
    }

    Celsius getTemperature() override {
        auto now = boot_clock::now();
        // Do not query more often than once per second
        if (now - this->lastMeasurementTime >= 1s) {
            float temperature;
            auto err = ds18x20_measure(pin->getGpio(), sensor, false);
            if (err != ESP_OK) {
                LOGE("Error measuring DS18B20 temperature: %s", esp_err_to_name(err));
            } else {
                // Wait for conversion (12-bit needs 750ms)
                Task::delay(750ms);
                err = ds18x20_read_temperature(pin->getGpio(), sensor, &temperature);
                if (err == ESP_OK) {
                    lastTemperature = temperature;
                    lastMeasurementTime = now;
                } else {
                    LOGE("Error reading DS18B20 temperature: %s", esp_err_to_name(err));
                }
            }
        }
        return lastTemperature;
    }

private:
    const InternalPinPtr pin;
    onewire_addr_t sensor {};

    std::chrono::time_point<boot_clock> lastMeasurementTime;
    Celsius lastTemperature = std::numeric_limits<double>::quiet_NaN();
};

inline PeripheralFactory makeFactoryForDs18b20() {
    return makePeripheralFactory<ITemperatureSensor, Ds18B20SoilSensor, SinglePinSettings>(
        "environment:ds18b20",
        "environment",
        [](PeripheralInitParameters& params, const std::shared_ptr<SinglePinSettings>& settings) {
            auto sensor = std::make_shared<Ds18B20SoilSensor>(
                params.name,
                settings->pin.get());
            params.registerFeature("temperature", [sensor](JsonObject& telemetryJson) {
                telemetryJson["value"] = sensor->getTemperature();
            });
            return sensor;
        });
}

}    // namespace farmhub::peripherals::environment
