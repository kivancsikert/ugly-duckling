#pragma once

#include <atomic>
#include <chrono>
#include <memory>

#include <Log.hpp>
#include <Pin.hpp>
#include <PwmManager.hpp>
#include <drivers/MotorDriver.hpp>
#include <utility>

using namespace std::chrono;

namespace farmhub::kernel::drivers {

/**
 * @brief Texas Instruments DRV883 dual motor driver.
 *
 * https://www.ti.com/lit/gpn/DRV8833
 */
class Drv8833Driver
    : public std::enable_shared_from_this<Drv8833Driver> {

public:
    static std::shared_ptr<Drv8833Driver> create(
        const std::shared_ptr<PwmManager>& pwm,
        const InternalPinPtr& ain1Pin,
        const InternalPinPtr& ain2Pin,
        const InternalPinPtr& bin1Pin,
        const InternalPinPtr& bin2Pin,
        const PinPtr& faultPin,
        const PinPtr& sleepPin) {
        auto driver = std::make_shared<Drv8833Driver>(faultPin, sleepPin);
        driver->initMotors(pwm, ain1Pin, ain2Pin, bin1Pin, bin2Pin);
        return driver;
    }

    std::shared_ptr<PwmMotorDriver> getMotorA() {
        return motorA;
    }

    std::shared_ptr<PwmMotorDriver> getMotorB() {
        return motorB;
    }

    // Note: on Ugly Duckling MK5, the DRV8874's PMODE is wired to 3.3V, so it's locked in PWM mode
    Drv8833Driver(const PinPtr& faultPin, const PinPtr& sleepPin)
        : faultPin(faultPin)
        , sleepPin(sleepPin) {

        LOGI("Initializing DRV8833 on pins fault = %s, sleep = %s",
            faultPin->getName().c_str(),
            sleepPin->getName().c_str());

        if (sleepPin != nullptr) {
            sleepPin->pinMode(Pin::Mode::Output);
        }
        faultPin->pinMode(Pin::Mode::Input);

        setSleepState(true);
    }

private:
    void initMotors(
        const std::shared_ptr<PwmManager>& pwm,
        const InternalPinPtr& ain1Pin,
        const InternalPinPtr& ain2Pin,
        const InternalPinPtr& bin1Pin,
        const InternalPinPtr& bin2Pin) {

        LOGI("Initializing DRV8833 motors on pins ain1 = %s, ain2 = %s, bin1 = %s, bin2 = %s",
            ain1Pin->getName().c_str(),
            ain2Pin->getName().c_str(),
            bin1Pin->getName().c_str(),
            bin2Pin->getName().c_str());
        motorA = std::make_shared<Drv8833MotorDriver>(shared_from_this(), pwm, ain1Pin, ain2Pin, sleepPin != nullptr);
        motorB = std::make_shared<Drv8833MotorDriver>(shared_from_this(), pwm, bin1Pin, bin2Pin, sleepPin != nullptr);
    }

    class Drv8833MotorDriver : public PwmMotorDriver {
    private:
        static constexpr uint32_t PWM_FREQ = 25000;
        static constexpr ledc_timer_bit_t PWM_RESOLUTION = LEDC_TIMER_10_BIT;

    public:
        Drv8833MotorDriver(
            const std::shared_ptr<Drv8833Driver>& driver,
            const std::shared_ptr<PwmManager>& pwm,
            const InternalPinPtr& in1Pin,
            const InternalPinPtr& in2Pin,
            bool canSleep)
            : driver(driver)
            , in1Channel(pwm->registerPin(in1Pin, PWM_FREQ, PWM_RESOLUTION))
            , in2Channel(pwm->registerPin(in2Pin, PWM_FREQ, PWM_RESOLUTION))
            , sleeping(canSleep) {
        }

        void drive(MotorPhase phase, double duty) override {
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
        const std::shared_ptr<Drv8833Driver> driver;
        const PwmPin& in1Channel;
        const PwmPin& in2Channel;

        bool sleeping;
    };

    void updateSleepState() {
        setSleepState(motorA->isSleeping() && motorB->isSleeping());
    }

    void setSleepState(bool sleep) {
        if (sleepPin != nullptr) {
            sleepPin->digitalWrite(sleep ? 0 : 1);
        }
    }

    std::shared_ptr<Drv8833MotorDriver> motorA;
    std::shared_ptr<Drv8833MotorDriver> motorB;
    const PinPtr faultPin;
    const PinPtr sleepPin;

    std::atomic<bool> sleeping { false };
};

}    // namespace farmhub::kernel::drivers
