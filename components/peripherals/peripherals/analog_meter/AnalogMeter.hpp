#pragma once

#include <chrono>
#include <utility>

#include <BootClock.hpp>
#include <Concurrent.hpp>
#include <Configuration.hpp>
#include <MovingAverage.hpp>
#include <Pin.hpp>
#include <Task.hpp>
#include <mqtt/MqttDriver.hpp>
#include <peripherals/Peripheral.hpp>

using namespace std::chrono;
using namespace std::chrono_literals;
using namespace farmhub::kernel;
using namespace farmhub::kernel::mqtt;

namespace farmhub::peripherals::analog_meter {

class AnalogMeter final
    : Named {
public:
    AnalogMeter(
        const std::string& name,
        const InternalPinPtr& pin,
        double offset,
        double multiplier,
        milliseconds measurementFrequency,
        std::size_t windowSize)
        : Named(name)
        , pin(pin)
        , value(windowSize) {

        LOGI("Initializing analog meter on pin %s",
            pin->getName().c_str());

        Task::loop(name, 3172, [this, measurementFrequency, offset, multiplier](Task& task) {
            auto measurement = this->pin.analogRead();
            if (measurement.has_value()) {
                double value = offset + measurement.value() * multiplier;
                LOGV("Analog value on '%s' measured at %.2f",
                    this->name.c_str(), value);
                this->value.record(value);
            }
            task.delayUntil(measurementFrequency);
        });
    }

    double getValue() {
        return value.getAverage();
    }

private:
    AnalogPin pin;
    MovingAverage<double> value;
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

    std::shared_ptr<Peripheral<EmptyConfiguration>> createPeripheral(const std::string& name, const std::shared_ptr<AnalogMeterDeviceConfig>& deviceConfig, const std::shared_ptr<MqttRoot>& /*mqttRoot*/, const PeripheralServices& services) override {
        auto meter = std::make_shared<AnalogMeter>(
            name,
            deviceConfig->pin.get(),
            deviceConfig->offset.get(),
            deviceConfig->multiplier.get(),
            deviceConfig->measurementFrequency.get(),
            deviceConfig->windowSize.get());
        services.telemetryCollector->registerProvider(deviceConfig->type.get(), name, [meter](JsonObject& telemetryJson) {
            telemetryJson["value"] = meter->getValue();
        });
        return std::make_shared<SimplePeripheral>(name, meter);
    }
};

}    // namespace farmhub::peripherals::analog_meter
