#pragma once

#include <Arduino.h>

#include <kernel/Log.hpp>

namespace farmhub::kernel {

// TODO Figure out what to do with low/high speed modes
//      See https://docs.espressif.com/projects/esp-idf/en/release-v4.2/esp32/api-reference/peripherals/ledc.html#ledc-high-and-low-speed-mode

// TODO Limit number of channels available
struct PwmPin {
    PwmPin(gpio_num_t pin, uint32_t freq, uint8_t resolutionBits)
        : pin(pin)
        , freq(freq)
        , resolutionBits(resolutionBits) {
    }

    PwmPin(const PwmPin& other)
        : PwmPin(other.pin, other.freq, other.resolutionBits) {
    }

    uint32_t constexpr maxValue() const {
        return (1 << resolutionBits) - 1;
    }

    void write(uint32_t value) const {
        ledcWrite(pin, value);
    }

    const gpio_num_t pin;
    const uint32_t freq;
    const uint8_t resolutionBits;
};

class PwmManager {
public:
    PwmPin registerPin(gpio_num_t pin, uint32_t freq, uint8_t resolutionBits = 8) {
        ledcAttach(pin, freq, resolutionBits);
        Log.debug("Registered PWM channel on pin %d with freq %ld and resolution %d",
            pin, freq, resolutionBits);
        return PwmPin(pin, freq, resolutionBits);
    }
};

}    // namespace farmhub::kernel
