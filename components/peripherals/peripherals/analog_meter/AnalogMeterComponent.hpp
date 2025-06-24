#pragma once

#include <chrono>

#include <BootClock.hpp>
#include <Concurrent.hpp>
#include <MovingAverage.hpp>
#include <Pin.hpp>
#include <Task.hpp>
#include <utility>

using namespace std::chrono;
using namespace std::chrono_literals;
using namespace farmhub::kernel;
using namespace farmhub::kernel::mqtt;

namespace farmhub::peripherals::analog_meter {

class AnalogMeterComponent final
    : Named {
public:
    AnalogMeterComponent(
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

}    // namespace farmhub::peripherals::analog_meter
