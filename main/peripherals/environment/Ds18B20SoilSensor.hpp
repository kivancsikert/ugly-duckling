#pragma once

#include <memory>

#include <Arduino.h>

#include <ds18b20.h>
#include <onewire_bus.h>

#include <kernel/Component.hpp>
#include <kernel/Configuration.hpp>
#include <peripherals/Peripheral.hpp>
#include <peripherals/SinglePinDeviceConfig.hpp>

using namespace farmhub::kernel;
using namespace farmhub::kernel::mqtt;
using namespace farmhub::peripherals;
using std::make_unique;
using std::unique_ptr;
namespace farmhub::peripherals::environment {

/**
 * @brief Support for DS18B20 soil temperature sensor.
 *
 * Note: Needs a 4.7k pull-up resistor between the data and power lines.
 */
class Ds18B20SoilSensorComponent
    : public Component,
      public TelemetryProvider {
public:
    Ds18B20SoilSensorComponent(
        const String& name,
        shared_ptr<MqttRoot> mqttRoot,
        InternalPinPtr pin)
        : Component(name, mqttRoot) {

        LOGI("Initializing DS18B20 soil temperature sensor on pin %s",
            pin->getName().c_str());

        onewire_bus_handle_t bus;
        onewire_bus_config_t busConfig = {
            .bus_gpio_num = pin->getGpio(),
        };
        onewire_bus_rmt_config_t rmtConfig = {
            .max_rx_bytes = 10,    // 1byte ROM command + 8byte ROM number + 1byte device command
        };
        ESP_ERROR_CHECK(onewire_new_bus_rmt(&busConfig, &rmtConfig, &bus));

        LOGV("Locating DS18B20 sensors on bus...");
        int sensorCount = 0;
        // TODO How many slots do we need here actually?
        int maxSensors = 1;
        ds18b20_device_handle_t sensors[maxSensors];
        onewire_device_iter_handle_t iter = NULL;
        onewire_device_t nextOnewireDevice;
        ESP_ERROR_CHECK(onewire_new_device_iter(bus, &iter));

        while (sensorCount < maxSensors) {
            esp_err_t searchResult = onewire_device_iter_get_next(iter, &nextOnewireDevice);
            if (searchResult == ESP_OK) {
                ds18b20_config_t sensorConfig = {};
                // Check if the device is a DS18B20, if so, return its handle
                if (ds18b20_new_device(&nextOnewireDevice, &sensorConfig, &sensors[sensorCount]) == ESP_OK) {
                    LOGD("Found a DS18B20[%d], address: %016llX", sensorCount, nextOnewireDevice.address);
                    sensorCount++;
                } else {
                    LOGD("Found an unknown device, address: %016llX", nextOnewireDevice.address);
                }
            } else {
                throw PeripheralCreationException("Error searching for DS18B20 devices: " + String(esp_err_to_name(searchResult)));
            }
        }
        ESP_ERROR_CHECK(onewire_del_device_iter(iter));

        if (sensorCount == 0) {
            throw PeripheralCreationException("No DS18B20 sensors found on bus");
        }

        sensor = sensors[0];
    }

    void populateTelemetry(JsonObject& json) override {
        // TODO Get temperature in a task to avoid delaying reporting
        float temperature;
        ESP_ERROR_CHECK(ds18b20_trigger_temperature_conversion(sensor));
        ESP_ERROR_CHECK(ds18b20_get_temperature(sensor, &temperature));
        json["temperature"] = temperature;
    }

private:
    ds18b20_device_handle_t sensor;
};

class Ds18B20SoilSensor
    : public Peripheral<EmptyConfiguration> {
public:
    Ds18B20SoilSensor(const String& name, shared_ptr<MqttRoot> mqttRoot, InternalPinPtr pin)
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

    unique_ptr<Peripheral<EmptyConfiguration>> createPeripheral(const String& name, const SinglePinDeviceConfig& deviceConfig, shared_ptr<MqttRoot> mqttRoot, PeripheralServices& services) override {
        return make_unique<Ds18B20SoilSensor>(name, mqttRoot, deviceConfig.pin.get());
    }
};

}    // namespace farmhub::peripherals::environment
