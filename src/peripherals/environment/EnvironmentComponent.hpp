#pragma once

#include <Arduino.h>
#include <Wire.h>

#include <ArduinoLog.h>
#include <SHT31.h>

#include <kernel/Component.hpp>
#include <kernel/Telemetry.hpp>

#include <peripherals/I2CConfig.hpp>

using namespace farmhub::kernel;

namespace farmhub::peripherals::environment {

class Sht31Component
    : public Component,
      public TelemetryProvider {
public:
    Sht31Component(
        const String& name,
        shared_ptr<MqttDriver::MqttRoot> mqttRoot,
        I2CConfig config)
        : Component(name, mqttRoot)
        // TODO Add I2C manager to hand out wires
        , wire(1)
        , sht31(config.address, &wire) {

        // TODO Add commands to soft/hard reset the sensor
        // TODO Add configuration for fast / slow measurement
        // TODO Add a separate task to do measurements to unblock telemetry collection?

        Log.infoln("Initializing SHT31 environment sensor with %s", config.toString().c_str());
        if (!wire.begin(config.sda, config.scl, 100000L)) {
            Log.errorln("Failed to initialize I2C bus for SHT31 environment sensor");
            return;
        }
        if (!sht31.begin()) {
            Log.errorln("Failed to initialize SHT31 environment sensor: %d",
                sht31.getError());
            return;
        }
        if (!sht31.isConnected()) {
            Log.errorln("SHT31 environment sensor is not connected: %d",
                sht31.getError());
            return;
        }
        initialized = true;
    }

    void populateTelemetry(JsonObject& json) override {
        if (!initialized) {
            return;
        }
        if (!sht31.read()) {
            Log.errorln("Failed to read SHT31 environment sensor: %d",
                sht31.getError());
            return;
        }
        json["temperature"] = sht31.getTemperature();
        json["humidity"] = sht31.getHumidity();
    }

private:
    TwoWire wire;
    SHT31 sht31;
    bool initialized = false;
};

}    // namespace farmhub::peripherals::environment
