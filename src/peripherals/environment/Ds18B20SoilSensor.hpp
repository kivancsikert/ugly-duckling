#pragma once

#include <memory>

#include <Arduino.h>

#include <DallasTemperature.h>
#include <OneWire.h>

#include <peripherals/Peripheral.hpp>
#include <kernel/Configuration.hpp>
#include <kernel/Component.hpp>
#include <kernel/drivers/MqttDriver.hpp>
#include <peripherals/SinglePinDeviceConfig.hpp>

using namespace farmhub::kernel;
using namespace farmhub::kernel::drivers;
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
        shared_ptr<MqttDriver::MqttRoot> mqttRoot,
        gpio_num_t pin)
        : Component(name, mqttRoot) {

        Log.infoln("Initializing DS18B20 soil temperature sensor on pin %d",
            pin);

        oneWire.begin(pin);

        // locate devices on the bus
        Log.verboseln("Locating devices...");
        sensors.begin();
        Log.traceln("Found %d devices, parasitic power is %s",
            sensors.getDeviceCount(),
            sensors.isParasitePowerMode() ? "ON" : "OFF");

        DeviceAddress thermometer;
        if (!sensors.getAddress(thermometer, 0)) {
            throw PeripheralCreationException("unable to find address for device");
        }

        // show the addresses we found on the bus
        Log.traceln("Device 0 Address: %s",
            toStringAddress(thermometer).c_str());
    }

    void populateTelemetry(JsonObject& json) override {
        if (!sensors.requestTemperaturesByIndex(0)) {
            Log.errorln("Failed to get temperature from DS18B20 sensor");
            return;
        }
        float temperature = sensors.getTempCByIndex(0);
        if (temperature == DEVICE_DISCONNECTED_C) {
            Log.errorln("Failed to get temperature from DS18B20 sensor");
            return;
        }
        json["temperature"] = temperature;
    }

private:

    OneWire oneWire;

    // Pass our oneWire reference to Dallas Temperature.
    DallasTemperature sensors { &oneWire };

    String toStringAddress(DeviceAddress address) {
        char result[17];
        for (int i = 0; i < 8; i++) {
            sprintf(&result[i * 2], "%02X", address[i]);
        }
        result[16] = '\0';
        return result;
    }
};

class Ds18B20SoilSensor
    : public Peripheral<EmptyConfiguration> {
public:
    Ds18B20SoilSensor(const String& name, shared_ptr<MqttDriver::MqttRoot> mqttRoot, gpio_num_t pin)
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

    unique_ptr<Peripheral<EmptyConfiguration>> createPeripheral(const String& name, const SinglePinDeviceConfig& deviceConfig, shared_ptr<MqttDriver::MqttRoot> mqttRoot, PeripheralServices& services) override {
        return make_unique<Ds18B20SoilSensor>(name, mqttRoot, deviceConfig.pin.get());
    }
};

}    // namespace farmhub::peripherals::environment
