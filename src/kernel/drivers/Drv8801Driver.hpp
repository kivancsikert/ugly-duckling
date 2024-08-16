#pragma once

#include <atomic>
#include <chrono>

#include <Arduino.h>

#include <kernel/PwmManager.hpp>
#include <kernel/drivers/CurrentSenseDriver.hpp>
#include <kernel/drivers/MotorDriver.hpp>

using namespace std::chrono;

namespace farmhub::kernel::drivers {

/**
 * @brief Texas Instruments DRV8801 motor driver.
 *
 * https://www.ti.com/lit/gpn/DRV8801
 */
class Drv8801Driver
    : public CurrentSensingMotorDriver {

private:
    const uint32_t PWM_FREQ = 25000;     // 25kHz
    const uint8_t PWM_RESOLUTION = 8;    // 8 bit

public:
    // Note: on Ugly Duckling MK5, the DRV8874's PMODE is wired to 3.3V, so it's locked in PWM mode
    Drv8801Driver(
        PwmManager& pwm,
        gpio_num_t enablePin,
        gpio_num_t phasePin,
        gpio_num_t mode1Pin,
        gpio_num_t mode2Pin,
        gpio_num_t currentPin,
        gpio_num_t faultPin,
        gpio_num_t sleepPin)
        : enablePin(enablePin)
        , phaseChannel(pwm.registerChannel(phasePin, PWM_FREQ, PWM_RESOLUTION))
        , currentPin(currentPin)
        , faultPin(faultPin)
        , sleepPin(sleepPin) {

        Log.info("Initializing DRV8801 on pins enable = %d, phase = %d, fault = %d, sleep = %d, mode1 = %d, mode2 = %d, current = %d",
            enablePin, phasePin, faultPin, sleepPin, mode1Pin, mode2Pin, currentPin);

        pinMode(enablePin, OUTPUT);
        pinMode(mode1Pin, OUTPUT);
        pinMode(mode2Pin, OUTPUT);
        pinMode(sleepPin, OUTPUT);
        pinMode(faultPin, INPUT);
        pinMode(currentPin, INPUT);

        // TODO Allow using the DRV8801 in other modes
        digitalWrite(mode1Pin, HIGH);
        digitalWrite(mode2Pin, HIGH);

        sleep();
    }

    void drive(MotorPhase phase, double duty = 1) override {
        if (duty == 0) {
            Log.debug("Stopping");
            sleep();
            digitalWrite(enablePin, LOW);
            return;
        }
        wakeUp();
        digitalWrite(enablePin, HIGH);

        int direction = (phase == MotorPhase::FORWARD ? 1 : -1);
        int dutyValue = phaseChannel.maxValue() / 2 + direction * (int) (phaseChannel.maxValue() / 2 * duty);
        Log.debug("Driving motor %s at %d%%",
            phase == MotorPhase::FORWARD ? "forward" : "reverse",
            (int) (duty * 100));

        phaseChannel.write(dutyValue);
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

    double readCurrent() override {
        return analogRead(currentPin) / 4096.0;
    }

private:
    const gpio_num_t enablePin;
    const PwmChannel phaseChannel;
    const gpio_num_t currentPin;
    const gpio_num_t faultPin;
    const gpio_num_t sleepPin;

    std::atomic<bool> sleeping { false };
};

}    // namespace farmhub::kernel::drivers
