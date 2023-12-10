#pragma once

#include <atomic>
#include <chrono>

#include <Arduino.h>

#include <kernel/PwmManager.hpp>
#include <kernel/drivers/MotorDriver.hpp>

using namespace std::chrono;

namespace farmhub { namespace kernel { namespace drivers {

/**
 * @brief Texas Instruments DRV8874 motor driver.
 */
class Drv8874Driver
    : public PwmMotorDriver {

private:
    const uint32_t PWM_FREQ = 25000;     // 25kHz
    const uint8_t PWM_RESOLUTION = 8;    // 8 bit

public:
    // Note: on Ugly Duckling MK5, the DRV8874's PMODE is wired to 3.3V, so it's locked in PWM mode
    Drv8874Driver(
        PwmManager& pwm,
        gpio_num_t in1Pin,
        gpio_num_t in2Pin,
        gpio_num_t currentPin,
        gpio_num_t faultPin,
        gpio_num_t sleepPin)
        : in1Channel(pwm.registerChannel(in1Pin, PWM_FREQ, PWM_RESOLUTION))
        , in2Channel(pwm.registerChannel(in2Pin, PWM_FREQ, PWM_RESOLUTION))
        , currentPin(currentPin)
        , faultPin(faultPin)
        , sleepPin(sleepPin) {

        Serial.printf("Initializing DRV8874 on pins in1 = %d, in2 = %d, fault = %d, sleep = %d, current = %d\n",
            in1Pin, in2Pin, faultPin, sleepPin, currentPin);

        pinMode(sleepPin, OUTPUT);
        pinMode(faultPin, INPUT);
        pinMode(currentPin, INPUT);

        sleep();
    }

    virtual void drive(bool phase, double duty = 1) override {
        int dutyValue = in1Channel.maxValue() / 2 + (int) (in1Channel.maxValue() / 2 * duty);
        Serial.printf("Driving valve %s at %.2f%%\n",
            phase ? "forward" : "reverse",
            duty * 100);

        if (phase) {
            in1Channel.write(dutyValue);
            in2Channel.write(0);
        } else {
            in1Channel.write(0);
            in2Channel.write(dutyValue);
        }
    }

    void sleep() {
        digitalWrite(sleepPin, LOW);
        sleeping = true;
    }

    void wakeUp() {
        digitalWrite(sleepPin, HIGH);
        sleeping = false;
    }

    bool isSleeping() const {
        return sleeping;
    }

private:
    const PwmChannel in1Channel;
    const PwmChannel in2Channel;
    const gpio_num_t faultPin;
    const gpio_num_t sleepPin;
    const gpio_num_t currentPin;

    std::atomic<bool> sleeping { false };
};

}}}    // namespace farmhub::kernel::drivers
