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

class Sht3xComponent
    : public Component,
      public TelemetryProvider {
public:
    Sht3xComponent(
        const String& name,
        shared_ptr<MqttDriver::MqttRoot> mqttRoot,
        I2CConfig config)
        : Component(name, mqttRoot)
        // TODO Add I2C manager to hand out wires
        , wire(1)
        , sht3x(config.address, &wire) {

        // TODO Add commands to soft/hard reset the sensor
        // TODO Add configuration for fast / slow measurement
        // TODO Add a separate task to do measurements to unblock telemetry collection?

        Log.infoln("Initializing SHT3s environment sensor with %s", config.toString().c_str());
        if (!wire.begin(config.sda, config.scl, 100000L)) {
            Log.errorln("Failed to initialize I2C bus for SHT3x environment sensor");
            return;
        }
        if (!sht3x.begin()) {
            Log.errorln("Failed to initialize SHT3x environment sensor: %d",
                sht3x.getError());
            return;
        }
        if (!sht3x.isConnected()) {
            Log.errorln("SHT3x environment sensor is not connected: %d",
                sht3x.getError());
            return;
        }
        initialized = true;
    }

    void populateTelemetry(JsonObject& json) override {
        if (!initialized) {
            return;
        }
        if (!sht3x.read()) {
            Log.errorln("Failed to read SHT3x environment sensor: %d",
                sht3x.getError());
            return;
        }
        json["temperature"] = sht3x.getTemperature();
        json["humidity"] = sht3x.getHumidity();
    }

private:
    TwoWire wire;
    SHT31 sht3x;
    bool initialized = false;
};

}    // namespace farmhub::peripherals::environment
