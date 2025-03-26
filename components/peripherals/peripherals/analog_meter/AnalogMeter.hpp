#pragma once

#include <chrono>

#include <Configuration.hpp>
#include <mqtt/MqttDriver.hpp>
#include <peripherals/Peripheral.hpp>
#include <peripherals/analog_meter/AnalogMeterComponent.hpp>

using namespace farmhub::kernel::mqtt;

namespace farmhub::peripherals::analog_meter {

class AnalogMeter
    : public Peripheral<EmptyConfiguration> {
public:
    AnalogMeter(
        const std::string& name,
        std::shared_ptr<MqttRoot> mqttRoot,
        InternalPinPtr pin,
        double offset,
        double multiplier,
        milliseconds measurementFrequency,
        std::size_t windowSize)
        : Peripheral<EmptyConfiguration>(name, mqttRoot)
        , meter(name, mqttRoot, pin, offset, multiplier, measurementFrequency, windowSize) {
        };

        void populateTelemetry(JsonObject& json) override {
            meter.populateTelemetry(json);
        }

private:
    AnalogMeterComponent meter;
};

class AnalogMeterDeviceConfig
    : public ConfigurationSection {
public:
    Property<InternalPinPtr> pin { this, "pin" };
    Property<double> offset { this, "offset", 0.0 };
    Property<double> multiplier { this, "multiplier", 1.0 };
    Property<milliseconds> measurementFrequency { this, "measurementFrequency", 1s };
    Property<std::size_t> windowSize { this, "windowSize", 1 };
};

class AnalogMeterFactory
    : public PeripheralFactory<AnalogMeterDeviceConfig, EmptyConfiguration> {
public:
    AnalogMeterFactory()
        : PeripheralFactory<AnalogMeterDeviceConfig, EmptyConfiguration>("analog-meter") {
    }

    std::unique_ptr<Peripheral<EmptyConfiguration>> createPeripheral(const std::string& name, const std::shared_ptr<AnalogMeterDeviceConfig> deviceConfig, std::shared_ptr<MqttRoot> mqttRoot, const PeripheralServices& services) override {
        return std::make_unique<AnalogMeter>(
            name,
            mqttRoot,
            deviceConfig->pin.get(),
            deviceConfig->offset.get(),
            deviceConfig->multiplier.get(),
            deviceConfig->measurementFrequency.get(),
            deviceConfig->windowSize.get());
    }
};

}    // namespace farmhub::peripherals::analog_meter
