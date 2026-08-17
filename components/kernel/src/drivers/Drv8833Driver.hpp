#pragma once

#include <memory>

#include <Log.hpp>
#include <Pin.hpp>
#include <PwmManager.hpp>
#include <drivers/MotorDriver.hpp>
#include <drivers/SharedEnable.hpp>

namespace cornucopia::ugly_duckling::kernel::drivers {

/**
 * @brief Texas Instruments DRV8833 dual motor driver.
 *
 * https://www.ti.com/lit/gpn/DRV8833
 */
class Drv8833Driver {

public:
    static std::shared_ptr<Drv8833Driver> create(
        const std::shared_ptr<PwmManager>& pwm,
        const InternalPinPtr& ain1Pin,
        const InternalPinPtr& ain2Pin,
        const InternalPinPtr& bin1Pin,
        const InternalPinPtr& bin2Pin,
        const PinPtr& faultPin,
        const std::shared_ptr<SharedEnable>& enable,
        bool reverse = false) {
        return std::make_shared<Drv8833Driver>(pwm, ain1Pin, ain2Pin, bin1Pin, bin2Pin, faultPin, enable, reverse);
    }

    std::shared_ptr<PwmMotorDriver> getMotorA() {
        return motorA;
    }

    std::shared_ptr<PwmMotorDriver> getMotorB() {
        return motorB;
    }

    Drv8833Driver(
        const std::shared_ptr<PwmManager>& pwm,
        const InternalPinPtr& ain1Pin,
        const InternalPinPtr& ain2Pin,
        const InternalPinPtr& bin1Pin,
        const InternalPinPtr& bin2Pin,
        const PinPtr& faultPin,
        const std::shared_ptr<SharedEnable>& enable,
        bool reverse = false)
        : faultPin(faultPin) {

        LOGI("Initializing motor driver on pin fault = %s",
            faultPin->getName().c_str());

        faultPin->pinMode(Pin::Mode::Input);

        LOGI("Initializing motors on pins ain1 = %s, ain2 = %s, bin1 = %s, bin2 = %s",
            ain1Pin->getName().c_str(),
            ain2Pin->getName().c_str(),
            bin1Pin->getName().c_str(),
            bin2Pin->getName().c_str());
        motorA = std::make_shared<Drv8833MotorDriver>(pwm, ain1Pin, ain2Pin, enable, reverse);
        motorB = std::make_shared<Drv8833MotorDriver>(pwm, bin1Pin, bin2Pin, enable, reverse);
    }

private:
    class Drv8833MotorDriver : public PwmMotorDriver {
    private:
        static constexpr uint32_t PWM_FREQ = 25000;
        static constexpr ledc_timer_bit_t PWM_RESOLUTION = LEDC_TIMER_10_BIT;

    public:
        Drv8833MotorDriver(
            const std::shared_ptr<PwmManager>& pwm,
            const InternalPinPtr& in1Pin,
            const InternalPinPtr& in2Pin,
            const std::shared_ptr<SharedEnable>& enable,
            bool reverse)
            : forwardChannel(pwm->registerPin(reverse ? in1Pin : in2Pin, PWM_FREQ, PWM_RESOLUTION))
            , reverseChannel(pwm->registerPin(reverse ? in2Pin : in1Pin, PWM_FREQ, PWM_RESOLUTION))
            , enableHandle(enable->createHandle()) {
        }

        void drive(MotorPhase phase, double duty) override {
            int dutyValue = static_cast<int>((forwardChannel.maxValue() + forwardChannel.maxValue() * duty) / 2);
            LOGD("Driving motor %s on pins %s/%s at %d%%",
                phase == MotorPhase::Forward ? "forward" : "reverse",
                forwardChannel.getName().c_str(),
                reverseChannel.getName().c_str(),
                (int) (duty * 100));

            switch (phase) {
                case MotorPhase::Forward:
                    forwardChannel.write(dutyValue);
                    reverseChannel.write(0);
                    break;
                case MotorPhase::Reverse:
                    forwardChannel.write(0);
                    reverseChannel.write(dutyValue);
                    break;
            }

            if (duty == 0) {
                enableHandle.release();
            } else {
                enableHandle.acquire();
            }
        }

    private:
        const PwmPin& forwardChannel;
        const PwmPin& reverseChannel;
        SharedEnable::Handle enableHandle;
    };

    std::shared_ptr<Drv8833MotorDriver> motorA;
    std::shared_ptr<Drv8833MotorDriver> motorB;
    const PinPtr faultPin;
};

}    // namespace cornucopia::ugly_duckling::kernel::drivers
