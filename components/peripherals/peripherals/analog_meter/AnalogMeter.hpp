#pragma once

#include <chrono>

#include <Configuration.hpp>
#include <mqtt/MqttDriver.hpp>
#include <peripherals/Peripheral.hpp>
#include <peripherals/analog_meter/AnalogMeterComponent.hpp>
#include <utility>

using namespace farmhub::kernel::mqtt;

namespace farmhub::peripherals::analog_meter {

class AnalogMeterFactory;
class AnalogMeter
    : public Peripheral<EmptyConfiguration> {
public:
    AnalogMeter(
        const std::string& name,
        const InternalPinPtr& pin,
        double offset,
        double multiplier,
        milliseconds measurementFrequency,
        std::size_t windowSize)
        : Peripheral<EmptyConfiguration>(name)
        , meter(name, pin, offset, multiplier, measurementFrequency, windowSize) {
        };


private:
    AnalogMeterComponent meter;
    friend class AnalogMeterFactory;
};

class AnalogMeterDeviceConfig
    : public ConfigurationSection {
public:
    Property<std::string> type { this, "type", "analog-meter" };
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

    std::shared_ptr<Peripheral<EmptyConfiguration>> createPeripheral(const std::string& name, const std::shared_ptr<AnalogMeterDeviceConfig> deviceConfig, std::shared_ptr<MqttRoot>  /*mqttRoot*/, const PeripheralServices& services) override {
        auto peripheral = std::make_shared<AnalogMeter>(
            name,
            deviceConfig->pin.get(),
            deviceConfig->offset.get(),
            deviceConfig->multiplier.get(),
            deviceConfig->measurementFrequency.get(),
            deviceConfig->windowSize.get());
        services.telemetryCollector->registerProvider(deviceConfig->type.get(), name, [peripheral](JsonObject& telemetryJson) {
            telemetryJson["value"] = peripheral->meter.getValue();
        });
        return peripheral;
    }
};

}    // namespace farmhub::peripherals::analog_meter
