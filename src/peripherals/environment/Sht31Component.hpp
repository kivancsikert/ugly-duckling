#pragma once

#include <Arduino.h>
#include <Wire.h>

#include <ArduinoLog.h>
#include <SHT31.h>

#include <peripherals/Peripheral.hpp>
#include <kernel/Component.hpp>
#include <kernel/Telemetry.hpp>

#include <peripherals/I2CConfig.hpp>

using namespace farmhub::kernel;
using namespace farmhub::peripherals;

namespace farmhub::peripherals::environment {

class Sht31Component
    : public Component,
      public TelemetryProvider {
public:
    Sht31Component(
        const String& name,
        const String& sensorType,
        shared_ptr<MqttDriver::MqttRoot> mqttRoot,
        I2CConfig config)
        : Component(name, mqttRoot)
        // TODO Add I2C manager to hand out wires
        , wire(1)
        , sensor(config.address, &wire) {

        // TODO Add commands to soft/hard reset the sensor
        // TODO Add configuration for fast / slow measurement
        // TODO Add a separate task to do measurements to unblock telemetry collection?

        Log.infoln("Initializing %s environment sensor with %s",
            sensorType.c_str(), config.toString().c_str());

        if (!wire.begin(config.sda, config.scl, 100000L)) {
            throw PeripheralCreationException(name,
                "Failed to initialize I2C bus for environment sensor");
        }
        if (!sensor.begin()) {
            throw PeripheralCreationException(name,
                "Failed to initialize environment sensor: " + String(sensor.getError()));
        }
        if (!sensor.isConnected()) {
            throw PeripheralCreationException(name,
                "Environment sensor is not connected: " + String(sensor.getError()));
        }
    }

    void populateTelemetry(JsonObject& json) override {
        if (!sensor.read()) {
            Log.errorln("Failed to read SHT3x environment sensor: %d",
                sensor.getError());
            return;
        }
        json["temperature"] = sensor.getTemperature();
        json["humidity"] = sensor.getHumidity();
    }

private:
    TwoWire wire;
    SHT31 sensor;
};

}    // namespace farmhub::peripherals::environment
