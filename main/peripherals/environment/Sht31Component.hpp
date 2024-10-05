#pragma once

#include <Arduino.h>

#include <SHT31.h>

#include <kernel/Component.hpp>
#include <kernel/I2CManager.hpp>
#include <kernel/Log.hpp>
#include <kernel/Telemetry.hpp>

#include <peripherals/I2CConfig.hpp>
#include <peripherals/Peripheral.hpp>

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
        I2CManager& i2c,
        I2CConfig config)
        : Component(name, mqttRoot)
        , sensor(config.address, &i2c.getWireFor(config)) {

        // TODO Add commands to soft/hard reset the sensor
        // TODO Add configuration for fast / slow measurement
        // TODO Add a separate task to do measurements to unblock telemetry collection?

        Log.info("Initializing %s environment sensor with %s",
            sensorType.c_str(), config.toString().c_str());

        if (!sensor.begin()) {
            throw PeripheralCreationException("Failed to initialize environment sensor: " + String(sensor.getError()));
        }
        if (!sensor.isConnected()) {
            throw PeripheralCreationException("Environment sensor is not connected: " + String(sensor.getError()));
        }
    }

    void populateTelemetry(JsonObject& json) override {
        if (!sensor.read()) {
            Log.error("Failed to read SHT3x environment sensor: %d",
                sensor.getError());
            return;
        }
        json["temperature"] = sensor.getTemperature();
        json["humidity"] = sensor.getHumidity();
    }

private:
    SHT31 sensor;
};

}    // namespace farmhub::peripherals::environment
