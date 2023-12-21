#pragma once

#include <Arduino.h>

#include <ArduinoLog.h>

namespace farmhub { namespace kernel {

// TODO Figure out what to do with low/high speed modes
//      See https://docs.espressif.com/projects/esp-idf/en/release-v4.2/esp32/api-reference/peripherals/ledc.html#ledc-high-and-low-speed-mode

// TODO Limit number of channels available
struct PwmChannel {
    PwmChannel(uint8_t channel, gpio_num_t pin, uint32_t freq, uint8_t resolutionBits)
        : channel(channel)
        , pin(pin)
        , freq(freq)
        , resolutionBits(resolutionBits) {
    }

    PwmChannel(const PwmChannel& other)
        : PwmChannel(other.channel, other.pin, other.freq, other.resolutionBits) {
    }

    uint32_t constexpr maxValue() const {
        return (1 << resolutionBits) - 1;
    }

    void write(uint32_t value) const {
        ledcWrite(channel, value);
    }

    const uint8_t channel;
    const gpio_num_t pin;
    const uint32_t freq;
    const uint8_t resolutionBits;
};

class PwmManager {
public:
    PwmChannel registerChannel(gpio_num_t pin, uint32_t freq, uint8_t resolutionBits = 8) {
        uint8_t channel = nextChannel++;
        pinMode(pin, OUTPUT);
        ledcAttachPin(pin, channel);
        ledcSetup(channel, freq, resolutionBits);
        Log.traceln("Registered PWM channel %d on pin %d with freq %d and resolution %d",
            channel, pin, freq, resolutionBits);
        return PwmChannel(channel, pin, freq, resolutionBits);
    }

private:
    uint8_t nextChannel = 0;
};

}}    // namespace farmhub::kernel
