#pragma once

#include <chrono>
#include <utility>

#include <BootClock.hpp>
#include <Concurrent.hpp>
#include <Configuration.hpp>
#include <MovingAverage.hpp>
#include <Named.hpp>
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
    : Peripheral {
public:
    AnalogMeter(
        const std::string& name,
        const InternalPinPtr& pin,
        double offset,
        double multiplier,
        milliseconds measurementFrequency,
        std::size_t windowSize)
        : Peripheral(name)
        , pin(pin)
        , value(windowSize) {

        LOGI("Initializing analog meter on pin %s",
            pin->getName().c_str());

        Task::loop(name, 3072, [this, measurementFrequency, offset, multiplier](Task& task) {
            auto measurement = this->pin.analogRead();
            if (measurement.has_value()) {
                double value = offset + (measurement.value() * multiplier);
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

class AnalogMeterSettings
    : public ConfigurationSection {
public:
    Property<std::string> type { this, "type", "analog-meter" };
    Property<InternalPinPtr> pin { this, "pin" };
    Property<double> offset { this, "offset", 0.0 };
    Property<double> multiplier { this, "multiplier", 1.0 };
    Property<milliseconds> measurementFrequency { this, "measurementFrequency", 1s };
    Property<std::size_t> windowSize { this, "windowSize", 1 };
};

inline PeripheralFactory makeFactory() {
    return makePeripheralFactory<AnalogMeter, AnalogMeter, AnalogMeterSettings>(
        "analog-meter",
        "analog-meter",
        [](PeripheralInitParameters& params, const std::shared_ptr<AnalogMeterSettings>& settings) {
            auto meter = std::make_shared<AnalogMeter>(
                params.name,
                settings->pin.get(),
                settings->offset.get(),
                settings->multiplier.get(),
                settings->measurementFrequency.get(),
                settings->windowSize.get());

            params.registerFeature(settings->type.get(), [meter](JsonObject& telemetryJson) {
                telemetryJson["value"] = meter->getValue();
            });

            return meter;
        });
}

}    // namespace farmhub::peripherals::analog_meter
