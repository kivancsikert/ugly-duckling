#pragma once

#include <memory>

#include <ds18x20.h>

#include <Component.hpp>
#include <Configuration.hpp>
#include <peripherals/Peripheral.hpp>
#include <peripherals/SinglePinDeviceConfig.hpp>
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
class Ds18B20SoilSensorComponent final
    : public Component,
      public TelemetryProvider {
public:
    Ds18B20SoilSensorComponent(
        const std::string& name,
        std::shared_ptr<MqttRoot> mqttRoot,
        const InternalPinPtr& pin)
        : Component(name, std::move(mqttRoot))
        , pin(pin) {

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

    void populateTelemetry(JsonObject& json) override {
        // TODO Get temperature in a task to avoid delaying reporting
        float temperature;
        ESP_ERROR_THROW(ds18x20_measure_and_read_multi(pin->getGpio(), &sensor, 1, &temperature));
        json["temperature"] = temperature;
    }

private:
    const InternalPinPtr pin;
    onewire_addr_t sensor {};
};

class Ds18B20SoilSensor
    : public Peripheral<EmptyConfiguration> {
public:
    Ds18B20SoilSensor(const std::string& name, const std::shared_ptr<MqttRoot>& mqttRoot, const InternalPinPtr& pin)
        : Peripheral<EmptyConfiguration>(name, mqttRoot)
        , sensor(name, mqttRoot, pin) {
    }

    void populateTelemetry(JsonObject& telemetryJson) override {
        sensor.populateTelemetry(telemetryJson);
    }

private:
    Ds18B20SoilSensorComponent sensor;
};

class Ds18B20SoilSensorFactory
    : public PeripheralFactory<SinglePinDeviceConfig, EmptyConfiguration> {
public:
    Ds18B20SoilSensorFactory()
        : PeripheralFactory<SinglePinDeviceConfig, EmptyConfiguration>("environment:ds18b20", "environment") {
    }

    std::unique_ptr<Peripheral<EmptyConfiguration>> createPeripheral(const std::string& name, const std::shared_ptr<SinglePinDeviceConfig> deviceConfig, std::shared_ptr<MqttRoot> mqttRoot, const PeripheralServices&  /*services*/) override {
        return std::make_unique<Ds18B20SoilSensor>(name, mqttRoot, deviceConfig->pin.get());
    }
};

}    // namespace farmhub::peripherals::environment
