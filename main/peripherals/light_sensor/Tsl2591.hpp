#pragma once

#include <chrono>
#include <deque>
#include <memory>

#include <Arduino.h>
#include <Wire.h>

#include <Adafruit_Sensor.h>
#include <Adafruit_TSL2591.h>

#include <kernel/Component.hpp>
#include <kernel/Configuration.hpp>
#include <kernel/I2CManager.hpp>
#include <kernel/Telemetry.hpp>

#include <peripherals/I2CConfig.hpp>
#include <peripherals/Peripheral.hpp>
#include <peripherals/light_sensor/LightSensor.hpp>

using namespace std::chrono;
using namespace std::chrono_literals;

using namespace farmhub::kernel;
using namespace farmhub::peripherals;

namespace farmhub::peripherals::light_sensor {

class Tsl2591DeviceConfig
    : public I2CDeviceConfig {
public:
    Property<seconds> measurementFrequency { this, "measurementFrequency", 1s };
    Property<seconds> latencyInterval { this, "latencyInterval", 5s };
};

class Tsl2591Component
    : public LightSensorComponent {
public:
    Tsl2591Component(
        const String& name,
        shared_ptr<MqttDriver::MqttRoot> mqttRoot,
        I2CManager& i2c,
        I2CConfig config,
        seconds measurementFrequency,
        seconds latencyInterval)
        : LightSensorComponent(name, mqttRoot, measurementFrequency, latencyInterval) {

        LOGI("Initializing TSL2591 light sensor with %s",
            config.toString().c_str());

        if (!sensor.begin(&i2c.getWireFor(config), config.address)) {
            throw PeripheralCreationException("Failed to initialize TSL2591 light sensor");
        }

        // TODO Make these configurable
        sensor.setGain(TSL2591_GAIN_MED);
        sensor.setTiming(TSL2591_INTEGRATIONTIME_300MS);

        sensor_t sensorInfo;
        sensor.getSensor(&sensorInfo);
        LOGD("Found sensor: %s, driver version: %ld, unique ID: %ld, max value: %.2f lux, min value: %.2f lux, resolution: %.2f mlux",
            sensorInfo.name, sensorInfo.version, sensorInfo.sensor_id, sensorInfo.max_value, sensorInfo.min_value, sensorInfo.resolution * 1000);

        runLoop();
    }

protected:
    double readLightLevel() override {
        return sensor.getLuminosity(TSL2591_VISIBLE);
    }

private:
    Adafruit_TSL2591 sensor;
};

class Tsl2591
    : public Peripheral<EmptyConfiguration> {

public:
    Tsl2591(
        const String& name,
        shared_ptr<MqttDriver::MqttRoot> mqttRoot,
        I2CManager& i2c,
        const I2CConfig& config,
        seconds measurementFrequency,
        seconds latencyInterval)
        : Peripheral<EmptyConfiguration>(name, mqttRoot)
        , component(name, mqttRoot, i2c, config, measurementFrequency, latencyInterval) {
    }

    void populateTelemetry(JsonObject& telemetryJson) override {
        component.populateTelemetry(telemetryJson);
    }

private:
    Tsl2591Component component;
};

class Tsl2591Factory
    : public PeripheralFactory<Tsl2591DeviceConfig, EmptyConfiguration> {
public:
    Tsl2591Factory()
        : PeripheralFactory<Tsl2591DeviceConfig, EmptyConfiguration>("light-sensor:tsl2591", "light-sensor") {
    }

    unique_ptr<Peripheral<EmptyConfiguration>> createPeripheral(const String& name, const Tsl2591DeviceConfig& deviceConfig, shared_ptr<MqttDriver::MqttRoot> mqttRoot, PeripheralServices& services) override {
        I2CConfig i2cConfig = deviceConfig.parse(TSL2591_ADDR);
        return std::make_unique<Tsl2591>(name, mqttRoot, services.i2c, i2cConfig, deviceConfig.measurementFrequency.get(), deviceConfig.latencyInterval.get());
    }
};

}    // namespace farmhub::peripherals::light_sensor
