#pragma once

#include <Arduino.h>

namespace farmhub::kernel::drivers {
class CurrentSenseDriver {

public:
    virtual double readCurrent() = 0;
};

class SimpleCurrentSenseDriver
    : public CurrentSenseDriver {
public:
    SimpleCurrentSenseDriver(gpio_num_t pin, double scale = 4096)
        : pin(pin)
        , scale(scale) {
        pinMode(pin, INPUT);
    }

    double readCurrent() override {
        return analogRead(pin) / scale;
    }

private:
    gpio_num_t pin;
    double scale;
};
}    // namespace farmhub::kernel::drivers
