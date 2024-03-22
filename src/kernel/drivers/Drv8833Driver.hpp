#pragma once

#include <atomic>
#include <chrono>

#include <Arduino.h>

#include <kernel/PwmManager.hpp>
#include <kernel/drivers/MotorDriver.hpp>

using namespace std::chrono;

namespace farmhub::kernel::drivers {

/**
 * @brief Texas Instruments DRV883 dual motor driver.
 *
 * https://www.ti.com/lit/gpn/DRV8833
 */
class Drv8833Driver {

public:
    // Note: on Ugly Duckling MK5, the DRV8874's PMODE is wired to 3.3V, so it's locked in PWM mode
    Drv8833Driver(
        PwmManager& pwm,
        gpio_num_t ain1Pin,
        gpio_num_t ain2Pin,
        gpio_num_t bin1Pin,
        gpio_num_t bin2Pin,
        gpio_num_t faultPin,
        gpio_num_t sleepPin)
        : motorA(this, pwm, ain1Pin, ain2Pin, sleepPin != GPIO_NUM_NC)
        , motorB(this, pwm, bin1Pin, bin2Pin, sleepPin != GPIO_NUM_NC)
        , faultPin(faultPin)
        , sleepPin(sleepPin) {

        Log.infoln("Initializing DRV8833 on pins ain1 = %d, ain2 = %d, bin1 = %d, bin2 = %d, fault = %d, sleep = %d",
            ain1Pin, ain2Pin, bin1Pin, bin2Pin, faultPin, sleepPin);

        pinMode(sleepPin, OUTPUT);
        pinMode(faultPin, INPUT);

        updateSleepState();
    }

    PwmMotorDriver& getMotorA() {
        return motorA;
    }

    PwmMotorDriver& getMotorB() {
        return motorB;
    }

private:
    class Drv8833MotorDriver : public PwmMotorDriver {
    private:
        static constexpr uint32_t PWM_FREQ = 25000;      // 25kHz
        static constexpr uint8_t PWM_RESOLUTION = 10;    // 10 bit

    public:
        Drv8833MotorDriver(
            Drv8833Driver* driver,
            PwmManager& pwm,
            gpio_num_t in1Pin,
            gpio_num_t in2Pin,
            bool canSleep)
            : driver(driver)
            , in1Channel(pwm.registerChannel(in1Pin, PWM_FREQ, PWM_RESOLUTION))
            , in2Channel(pwm.registerChannel(in2Pin, PWM_FREQ, PWM_RESOLUTION))
            , canSleep(canSleep)
            , sleeping(canSleep) {
        }

        void drive(MotorPhase phase, double duty = 1) override {
            int dutyValue = static_cast<int>((in1Channel.maxValue() + in1Channel.maxValue() * duty) / 2);
            Log.traceln("Driving motor %s on pins %d/%d at %d%% (duty = %d)",
                phase == MotorPhase::FORWARD ? "forward" : "reverse",
                in1Channel.pin,
                in2Channel.pin,
                (int) (duty * 100),
                dutyValue);

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

            if (duty == 0) {
                sleep();
            } else {
                wakeUp();
            }
        }

        void sleep() {
            sleeping = true;
            driver->updateSleepState();
        }

        void wakeUp() {
            sleeping = false;
            driver->updateSleepState();
        }

        bool isSleeping() const {
            return sleeping;
        }

    private:
        Drv8833Driver* const driver;
        const PwmChannel in1Channel;
        const PwmChannel in2Channel;
        const bool canSleep;

        bool sleeping;
    };

    void updateSleepState() {
        setSleepState(motorA.isSleeping() && motorB.isSleeping());
    }

    void setSleepState(bool sleep) {
        if (sleepPin != GPIO_NUM_NC) {
            digitalWrite(sleepPin, sleep ? LOW : HIGH);
        }
    }

    Drv8833MotorDriver motorA;
    Drv8833MotorDriver motorB;
    const gpio_num_t faultPin;
    const gpio_num_t sleepPin;

    std::atomic<bool> sleeping { false };
};

}    // namespace farmhub::kernel::drivers
