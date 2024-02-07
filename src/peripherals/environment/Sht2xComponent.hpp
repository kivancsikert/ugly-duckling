#pragma once

#include <Arduino.h>
#include <Wire.h>

#include <ArduinoLog.h>
#include <SHT2x.h>

#include <devices/Peripheral.hpp>
#include <kernel/Component.hpp>
#include <kernel/Telemetry.hpp>

#include <peripherals/I2CConfig.hpp>

using namespace farmhub::devices;
using namespace farmhub::kernel;

namespace farmhub::peripherals::environment {

/**
 * @tparam TSensor Works with SHT2x or HTU2x
 */
template <typename TSensor>
class Sht2xComponent
    : public Component,
      public TelemetryProvider {
public:
    Sht2xComponent(
        const String& name,
        const String& sensorType,
        shared_ptr<MqttDriver::MqttRoot> mqttRoot,
        I2CConfig config)
        : Component(name, mqttRoot)
        // TODO Add I2C manager to hand out wires
        , wire(1)
        , sensor(&wire) {

        // TODO Add commands to soft/hard reset the sensor
        // TODO Add configuration for fast / slow measurement
        // TODO Add a separate task to do measurements to unblock telemetry collection?

        Log.infoln("Initializing %s environment sensor with %s",
            sensorType.c_str(), config.toString().c_str());

        if (!wire.begin(config.sda, config.scl)) {
            throw PeripheralCreationException(name,
                "failed to initialize I2C bus for environment sensor");
        }
        if (!sensor.begin()) {
            throw PeripheralCreationException(name,
                "failed to initialize environment sensor: 0x" + String(sensor.getError(), HEX));
        }
        if (!sensor.isConnected()) {
            throw PeripheralCreationException(name,
                "environment sensor is not connected: 0x" + String(sensor.getError(), HEX));
        }
    }

    void populateTelemetry(JsonObject& json) override {
        if (!sensor.read()) {
            Log.errorln("Failed to read environment sensor: %d",
                sensor.getError());
            return;
        }
        json["temperature"] = sensor.getTemperature();
        json["humidity"] = sensor.getHumidity();
    }

private:
    TwoWire wire;
    TSensor sensor;
};

}    // namespace farmhub::peripherals::environment
