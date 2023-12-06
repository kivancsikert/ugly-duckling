#pragma once

#include <Arduino.h>

namespace farmhub { namespace kernel { namespace drivers {

class BatteryDriver {
public:
    BatteryDriver(gpio_num_t pin, float voltageDividerRatio)
        : pin(pin)
        , voltageDividerRatio(voltageDividerRatio) {
        pinMode(pin, INPUT);
    }

    float getVoltage() {
        auto batteryLevel = analogRead(pin);
        return batteryLevel * 3.3 / 4096 * voltageDividerRatio;
    }

private:
    const gpio_num_t pin;
    const float voltageDividerRatio;
};

}}}    // namespace farmhub::kernel::drivers
