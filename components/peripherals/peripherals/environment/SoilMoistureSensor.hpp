#pragma once

#include <memory>

#include <Component.hpp>

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
    : public Component {
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

    double getMoisture() {
        std::optional<uint16_t> soilMoistureValue = pin.analogRead();
        if (!soilMoistureValue.has_value()) {
            LOGD("Failed to read soil moisture value");
            return std::numeric_limits<double>::quiet_NaN();
        }
        LOGV("Soil moisture value: %d",
            soilMoistureValue.value());

        const double run = waterValue - airValue;
        const double rise = 100;
        const double delta = soilMoistureValue.value() - airValue;
        double moisture = (delta * rise) / run;

        return moisture;
    }

private:
    const int airValue;
    const int waterValue;
    AnalogPin pin;
};

class SoilMoistureSensorFactory;

class SoilMoistureSensor
    : public Peripheral<EmptyConfiguration> {
public:
    SoilMoistureSensor(const std::string& name, const std::shared_ptr<MqttRoot>& mqttRoot, const std::shared_ptr<SoilMoistureSensorDeviceConfig>& config)
        : Peripheral<EmptyConfiguration>(name, mqttRoot)
        , sensor(name, mqttRoot, config) {
    }

private:
    SoilMoistureSensorComponent sensor;
    friend class SoilMoistureSensorFactory;
};

class SoilMoistureSensorFactory
    : public PeripheralFactory<SoilMoistureSensorDeviceConfig, EmptyConfiguration> {
public:
    SoilMoistureSensorFactory()
        : PeripheralFactory<SoilMoistureSensorDeviceConfig, EmptyConfiguration>("environment:soil-moisture", "environment") {
    }

    std::shared_ptr<Peripheral<EmptyConfiguration>> createPeripheral(const std::string& name, const std::shared_ptr<SoilMoistureSensorDeviceConfig> deviceConfig, std::shared_ptr<MqttRoot> mqttRoot, const PeripheralServices&  services) override {
        auto peripheral = std::make_shared<SoilMoistureSensor>(name, mqttRoot, deviceConfig);
        services.telemetryCollector->registerProvider("moisture", name, [peripheral](JsonObject& telemetryJson) {
            telemetryJson["value"] = peripheral->sensor.getMoisture();
        });
        return peripheral;
    }
};

}    // namespace farmhub::peripherals::environment
