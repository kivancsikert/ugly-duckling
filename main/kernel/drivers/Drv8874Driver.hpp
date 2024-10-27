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
        InternalPinPtr in1Pin,
        InternalPinPtr in2Pin,
        PinPtr currentPin,
        PinPtr faultPin,
        PinPtr sleepPin)
        : in1Channel(pwm.registerPin(in1Pin, PWM_FREQ, PWM_RESOLUTION))
        , in2Channel(pwm.registerPin(in2Pin, PWM_FREQ, PWM_RESOLUTION))
        , currentPin(currentPin)
        , faultPin(faultPin)
        , sleepPin(sleepPin) {

        Log.info("Initializing DRV8874 on pins in1 = %s, in2 = %s, fault = %s, sleep = %s, current = %s",
            in1Pin->getName().c_str(),
            in2Pin->getName().c_str(),
            faultPin->getName().c_str(),
            sleepPin->getName().c_str(),
            currentPin->getName().c_str());

        sleepPin->pinMode(OUTPUT);
        faultPin->pinMode(INPUT);
        currentPin->pinMode(INPUT);

        sleep();
    }

    void drive(MotorPhase phase, double duty = 1) override {
        if (duty == 0) {
            Log.debug("Stopping motor");
            sleep();
            return;
        }
        wakeUp();

        int dutyValue = in1Channel.maxValue() / 2 + (int) (in1Channel.maxValue() / 2 * duty);
        Log.debug("Driving motor %s at %d%%",
            phase == MotorPhase::FORWARD ? "forward" : "reverse",
            (int) (duty * 100));

        switch (phase) {
            case MotorPhase::FORWARD:
                in1Channel.write(dutyValue);
                in2Channel.write(0);
                break;
            case MotorPhase::REVERSE:
                in1Channel.write(0);
                in2Channel.write(dutyValue);
                break;
        }
    }

    void sleep() {
        sleepPin->digitalWrite(LOW);
        sleeping = true;
    }

    void wakeUp() {
        sleepPin->digitalWrite(HIGH);
        sleeping = false;
    }

    bool isSleeping() const {
        return sleeping;
    }

private:
    const PwmPin in1Channel;
    const PwmPin in2Channel;
    const PinPtr currentPin;
    const PinPtr faultPin;
    const PinPtr sleepPin;

    std::atomic<bool> sleeping { false };
};

}    // namespace farmhub::kernel::drivers
