#pragma once

#include <atomic>
#include <chrono>

#include <Arduino.h>

#include <kernel/PwmManager.hpp>
#include <kernel/drivers/MotorDriver.hpp>

using namespace std::chrono;

namespace farmhub::kernel::drivers {

/**
 * @brief Texas Instruments DRV8801 motor driver.
 *
 * https://www.ti.com/lit/gpn/DRV8801
 */
class Drv8801Driver
    : public PwmMotorDriver {

private:
    const uint32_t PWM_FREQ = 25000;     // 25kHz
    const uint8_t PWM_RESOLUTION = 8;    // 8 bit

public:
    // Note: on Ugly Duckling MK5, the DRV8874's PMODE is wired to 3.3V, so it's locked in PWM mode
    Drv8801Driver(
        PwmManager& pwm,
        PinPtr enablePin,
        InternalPinPtr phasePin,
        PinPtr mode1Pin,
        PinPtr mode2Pin,
        PinPtr currentPin,
        PinPtr faultPin,
        PinPtr sleepPin)
        : enablePin(enablePin)
        , phaseChannel(pwm.registerPin(phasePin, PWM_FREQ, PWM_RESOLUTION))
        , currentPin(currentPin)
        , faultPin(faultPin)
        , sleepPin(sleepPin) {

        LOGI("Initializing DRV8801 on pins enable = %s, phase = %s, fault = %s, sleep = %s, mode1 = %s, mode2 = %s, current = %s",
            enablePin->getName().c_str(),
            phasePin->getName().c_str(),
            faultPin->getName().c_str(),
            sleepPin->getName().c_str(),
            mode1Pin->getName().c_str(),
            mode2Pin->getName().c_str(),
            currentPin->getName().c_str());

        enablePin->pinMode(OUTPUT);
        mode1Pin->pinMode(OUTPUT);
        mode2Pin->pinMode(OUTPUT);
        sleepPin->pinMode(OUTPUT);
        faultPin->pinMode(INPUT);
        currentPin->pinMode(INPUT);

        // TODO Allow using the DRV8801 in other modes
        mode1Pin->digitalWrite(HIGH);
        mode2Pin->digitalWrite(HIGH);

        sleep();
    }

    void drive(MotorPhase phase, double duty = 1) override {
        if (duty == 0) {
            LOGD("Stopping");
            sleep();
            enablePin->digitalWrite(LOW);
            return;
        }
        wakeUp();
        enablePin->digitalWrite(HIGH);

        int direction = (phase == MotorPhase::FORWARD ? 1 : -1);
        int dutyValue = phaseChannel.maxValue() / 2 + direction * (int) (phaseChannel.maxValue() / 2 * duty);
        LOGD("Driving motor %s at %d%%",
            phase == MotorPhase::FORWARD ? "forward" : "reverse",
            (int) (duty * 100));

        phaseChannel.write(dutyValue);
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
    const PinPtr enablePin;
    const PwmPin phaseChannel;
    const PinPtr currentPin;
    const PinPtr faultPin;
    const PinPtr sleepPin;

    std::atomic<bool> sleeping { false };
};

}    // namespace farmhub::kernel::drivers
