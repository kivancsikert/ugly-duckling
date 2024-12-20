#pragma once

#include <atomic>
#include <chrono>

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
        InternalPinPtr ain1Pin,
        InternalPinPtr ain2Pin,
        InternalPinPtr bin1Pin,
        InternalPinPtr bin2Pin,
        PinPtr faultPin,
        PinPtr sleepPin)
        : motorA(this, pwm, ain1Pin, ain2Pin, sleepPin != nullptr)
        , motorB(this, pwm, bin1Pin, bin2Pin, sleepPin != nullptr)
        , faultPin(faultPin)
        , sleepPin(sleepPin) {

        LOGI("Initializing DRV8833 on pins ain1 = %s, ain2 = %s, bin1 = %s, bin2 = %s, fault = %s, sleep = %s",
            ain1Pin->getName().c_str(),
            ain2Pin->getName().c_str(),
            bin1Pin->getName().c_str(),
            bin2Pin->getName().c_str(),
            faultPin->getName().c_str(),
            sleepPin->getName().c_str());

        if (sleepPin != nullptr) {
            sleepPin->pinMode(Pin::Mode::Output);
        }
        faultPin->pinMode(Pin::Mode::Input);

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
        static constexpr uint32_t PWM_FREQ = 25000;
        static constexpr ledc_timer_bit_t PWM_RESOLUTION = LEDC_TIMER_10_BIT;

    public:
        Drv8833MotorDriver(
            Drv8833Driver* driver,
            PwmManager& pwm,
            InternalPinPtr in1Pin,
            InternalPinPtr in2Pin,
            bool canSleep)
            : driver(driver)
            , in1Channel(pwm.registerPin(in1Pin, PWM_FREQ, PWM_RESOLUTION))
            , in2Channel(pwm.registerPin(in2Pin, PWM_FREQ, PWM_RESOLUTION))
            , sleeping(canSleep) {
        }

        void drive(MotorPhase phase, double duty = 1) override {
            int dutyValue = static_cast<int>((in1Channel.maxValue() + in1Channel.maxValue() * duty) / 2);
            LOGD("Driving motor %s on pins %s/%s at %d%% (duty = %d)",
                phase == MotorPhase::FORWARD ? "forward" : "reverse",
                in1Channel.getName().c_str(),
                in2Channel.getName().c_str(),
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
        const PwmPin& in1Channel;
        const PwmPin& in2Channel;

        bool sleeping;
    };

    void updateSleepState() {
        setSleepState(motorA.isSleeping() && motorB.isSleeping());
    }

    void setSleepState(bool sleep) {
        if (sleepPin != nullptr) {
            sleepPin->digitalWrite(sleep ? 0 : 1);
        }
    }

    Drv8833MotorDriver motorA;
    Drv8833MotorDriver motorB;
    const PinPtr faultPin;
    const PinPtr sleepPin;

    std::atomic<bool> sleeping { false };
};

}    // namespace farmhub::kernel::drivers
