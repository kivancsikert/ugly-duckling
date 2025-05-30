#pragma once

#include <memory>

#include <Component.hpp>
#include <Telemetry.hpp>

#include <peripherals/Peripheral.hpp>
#include <utility>

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

class SoilMoistureSensorComponent final
    : public Component,
      public TelemetryProvider {
public:
    SoilMoistureSensorComponent(
        const std::string& name,
        std::shared_ptr<MqttRoot> mqttRoot,
        const std::shared_ptr<SoilMoistureSensorDeviceConfig>& config)
        : Component(name, std::move(mqttRoot))
        , airValue(config->air.get())
        , waterValue(config->water.get())
        , pin(config->pin.get()) {

        LOGI("Initializing soil moisture sensor on pin %s; air value: %d; water value: %d",
            pin.getName().c_str(), airValue, waterValue);
    }

    void populateTelemetry(JsonObject& json) override {
        std::optional<uint16_t> soilMoistureValue = pin.analogRead();
        if (!soilMoistureValue.has_value()) {
            LOGD("Failed to read soil moisture value");
            return;
        }
        LOGV("Soil moisture value: %d",
            soilMoistureValue.value());

        const double run = waterValue - airValue;
        const double rise = 100;
        const double delta = soilMoistureValue.value() - airValue;
        double moisture = (delta * rise) / run;

        json["moisture"] = moisture;
    }

private:
    const int airValue;
    const int waterValue;
    AnalogPin pin;
};

class SoilMoistureSensor
    : public Peripheral<EmptyConfiguration> {
public:
    SoilMoistureSensor(const std::string& name, const std::shared_ptr<MqttRoot>& mqttRoot, const std::shared_ptr<SoilMoistureSensorDeviceConfig>& config)
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

    std::unique_ptr<Peripheral<EmptyConfiguration>> createPeripheral(const std::string& name, const std::shared_ptr<SoilMoistureSensorDeviceConfig> deviceConfig, std::shared_ptr<MqttRoot> mqttRoot, const PeripheralServices& services) override {
        return std::make_unique<SoilMoistureSensor>(name, mqttRoot, deviceConfig);
    }
};

}    // namespace farmhub::peripherals::environment
