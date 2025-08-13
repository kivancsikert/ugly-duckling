#pragma once

#include <memory>

#include <Configuration.hpp>
#include <ds18x20.h>
#include <peripherals/Peripheral.hpp>
#include <peripherals/SinglePinSettings.hpp>
#include <utility>

using namespace farmhub::kernel;
using namespace farmhub::kernel::mqtt;
using namespace farmhub::peripherals;

namespace farmhub::peripherals::environment {

/**
 * @brief Support for DS18B20 soil temperature sensor.
 *
 * Note: Needs a 4.7k pull-up resistor between the data and power lines.
 */
class Ds18B20SoilSensor final {
public:
    explicit Ds18B20SoilSensor(
        const InternalPinPtr& pin)
        : pin(pin) {

        LOGI("Initializing DS18B20 soil temperature sensor on pin %s",
            pin->getName().c_str());

        gpio_set_pull_mode(pin->getGpio(), GPIO_PULLUP_ONLY);

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

    double getTemperature() {
        float temperature;
        ESP_ERROR_THROW(ds18x20_measure_and_read_multi(pin->getGpio(), &sensor, 1, &temperature));
        return temperature;
    }

private:
    const InternalPinPtr pin;
    onewire_addr_t sensor {};
};

inline PeripheralFactory makeFactoryForDs18b20() {
    return makePeripheralFactory<SinglePinSettings>(
        "environment:ds18b20",
        "environment",
        [](PeripheralInitParameters& params, const std::shared_ptr<SinglePinSettings>& settings) {
            auto sensor = std::make_shared<Ds18B20SoilSensor>(settings->pin.get());
            params.registerFeature("temperature", [sensor](JsonObject& telemetryJson) {
                telemetryJson["value"] = sensor->getTemperature();
            });
            return sensor;
        });
}

}    // namespace farmhub::peripherals::environment
