#pragma once

#include <memory>

#include <Arduino.h>
#include <Wire.h>

#include <kernel/Component.hpp>
#include <kernel/Telemetry.hpp>

#include <peripherals/Peripheral.hpp>

using namespace farmhub::kernel;
using namespace farmhub::peripherals;

namespace farmhub::peripherals::environment {

class SoilMoistureSensorDeviceConfig
    : public ConfigurationSection {
public:
    Property<InternalPinPtr> pin { this, "pin" };
    // These values need calibrating for each sensor
    Property<uint16_t> air { this, "air", 3000 };
    Property<uint16_t> water { this, "water", 1000 };
};

class SoilMoistureSensorComponent
    : public Component,
      public TelemetryProvider {
public:
    SoilMoistureSensorComponent(
        const String& name,
        shared_ptr<MqttDriver::MqttRoot> mqttRoot,
        const SoilMoistureSensorDeviceConfig& config)
        : Component(name, mqttRoot)
        , airValue(config.air.get())
        , waterValue(config.water.get())
        , pin(config.pin.get()) {

        LOGI("Initializing soil moisture sensor on pin %s; air value: %d; water value: %d",
            pin->getName().c_str(), airValue, waterValue);

        pin->pinMode(INPUT);
    }

    void populateTelemetry(JsonObject& json) override {
        uint16_t soilMoistureValue = pin->analogRead();
        LOGV("Soil moisture value: %d",
            soilMoistureValue);

        const double run = waterValue - airValue;
        const double rise = 100;
        const double delta = soilMoistureValue - airValue;
        double moisture = (delta * rise) / run;

        json["moisture"] = moisture;
    }

private:
    const int airValue;
    const int waterValue;
    InternalPinPtr pin;
};

class SoilMoistureSensor
    : public Peripheral<EmptyConfiguration> {
public:
    SoilMoistureSensor(const String& name, shared_ptr<MqttDriver::MqttRoot> mqttRoot, const SoilMoistureSensorDeviceConfig& config)
        : Peripheral<EmptyConfiguration>(name, mqttRoot)
        , sensor(name, mqttRoot, config) {
    }

    void populateTelemetry(JsonObject& telemetryJson) override {
        sensor.populateTelemetry(telemetryJson);
    }

private:
    SoilMoistureSensorComponent sensor;
};

class SoilMoistureSensorFactory
    : public PeripheralFactory<SoilMoistureSensorDeviceConfig, EmptyConfiguration> {
public:
    SoilMoistureSensorFactory()
        : PeripheralFactory<SoilMoistureSensorDeviceConfig, EmptyConfiguration>("environment:soil-moisture", "environment") {
    }

    unique_ptr<Peripheral<EmptyConfiguration>> createPeripheral(const String& name, const SoilMoistureSensorDeviceConfig& deviceConfig, shared_ptr<MqttDriver::MqttRoot> mqttRoot, PeripheralServices& services) override {
        return std::make_unique<SoilMoistureSensor>(name, mqttRoot, deviceConfig);
    }
};

}    // namespace farmhub::peripherals::environment
