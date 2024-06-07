#pragma once

#include <atomic>
#include <chrono>

#include <Arduino.h>

#include <kernel/PwmManager.hpp>
#include <kernel/drivers/MotorDriver.hpp>

using namespace std::chrono;

namespace farmhub::kernel::drivers {

/**
 * @brief Texas Instruments DRV8874 motor driver.
 *
 * https://www.ti.com/lit/gpn/DRV8874
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
        : in1Pin(pwm.registerPin(in1Pin, PWM_FREQ, PWM_RESOLUTION))
        , in2Pin(pwm.registerPin(in2Pin, PWM_FREQ, PWM_RESOLUTION))
        , currentPin(currentPin)
        , faultPin(faultPin)
        , sleepPin(sleepPin) {

        Log.info("Initializing DRV8874 on pins in1 = %d, in2 = %d, fault = %d, sleep = %d, current = %d",
            in1Pin, in2Pin, faultPin, sleepPin, currentPin);

        pinMode(sleepPin, OUTPUT);
        pinMode(faultPin, INPUT);
        pinMode(currentPin, INPUT);

        sleep();
    }

    void drive(MotorPhase phase, double duty = 1) override {
        if (duty == 0) {
            Log.debug("Stopping motor");
            sleep();
            return;
        }
        wakeUp();

        int dutyValue = in1Pin.maxValue() / 2 + (int) (in1Pin.maxValue() / 2 * duty);
        Log.debug("Driving motor %s at %d%%",
            phase == MotorPhase::FORWARD ? "forward" : "reverse",
            (int) (duty * 100));

        switch (phase) {
            case MotorPhase::FORWARD:
                in1Pin.write(dutyValue);
                in2Pin.write(0);
                break;
            case MotorPhase::REVERSE:
                in1Pin.write(0);
                in2Pin.write(dutyValue);
                break;
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
    const PwmPin in1Pin;
    const PwmPin in2Pin;
    const gpio_num_t currentPin;
    const gpio_num_t faultPin;
    const gpio_num_t sleepPin;

    std::atomic<bool> sleeping { false };
};

}    // namespace farmhub::kernel::drivers
