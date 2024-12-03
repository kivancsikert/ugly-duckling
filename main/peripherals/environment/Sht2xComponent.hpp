#pragma once

#include <Arduino.h>

#include <SHT2x.h>

#include <kernel/Component.hpp>
#include <kernel/I2CManager.hpp>
#include <kernel/Log.hpp>
#include <kernel/Telemetry.hpp>

#include <peripherals/I2CConfig.hpp>
#include <peripherals/Peripheral.hpp>

using namespace farmhub::kernel;
using namespace farmhub::peripherals;

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
        I2CManager& i2c,
        I2CConfig config)
        : Component(name, mqttRoot)
        , sensor(&i2c.getWireFor(config)) {

        // TODO Add commands to soft/hard reset the sensor
        // TODO Add configuration for fast / slow measurement
        // TODO Add a separate task to do measurements to unblock telemetry collection?

        LOGI("Initializing %s environment sensor with %s",
            sensorType.c_str(), config.toString().c_str());

        if (!sensor.begin()) {
            throw PeripheralCreationException("failed to initialize environment sensor: 0x" + String(sensor.getError(), HEX));
        }
        if (!sensor.isConnected()) {
            throw PeripheralCreationException("environment sensor is not connected: 0x" + String(sensor.getError(), HEX));
        }
    }

    void populateTelemetry(JsonObject& json) override {
        if (!sensor.read()) {
            LOGE("Failed to read environment sensor: %d",
                sensor.getError());
            return;
        }
        json["temperature"] = sensor.getTemperature();
        json["humidity"] = sensor.getHumidity();
    }

private:
    TSensor sensor;
};

}    // namespace farmhub::peripherals::environment
