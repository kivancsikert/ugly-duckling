#pragma once

#include <Arduino.h>
#include <Wire.h>

#include <ArduinoLog.h>
#include <DallasTemperature.h>
#include <OneWire.h>

#include <devices/Peripheral.hpp>
#include <kernel/Component.hpp>
#include <kernel/Telemetry.hpp>

using namespace farmhub::devices;
using namespace farmhub::kernel;

namespace farmhub::peripherals::environment {

class Ds18B20SoilSensorComponent
    : public Component,
      public TelemetryProvider {
public:
    Ds18B20SoilSensorComponent(
        const String& name,
        shared_ptr<MqttDriver::MqttRoot> mqttRoot,
        gpio_num_t pin)
        : Component(name, mqttRoot) {

        Log.infoln("Initializing DS18B20 environment sensor on pin %d",
            pin);

        pinMode(pin, INPUT_PULLUP);
        oneWire.begin(pin);

        // locate devices on the bus
        Log.verboseln("Locating devices...");
        sensors.begin();
        Log.traceln("Found %d devices, parasitic power is %s",
            sensors.getDeviceCount(),
            sensors.isParasitePowerMode() ? "ON" : "OFF");

        DeviceAddress thermometer;
        if (!sensors.getAddress(thermometer, 0)) {
            throw PeripheralCreationException(name, "unable to find address for device");
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

}    // namespace farmhub::peripherals::environment
