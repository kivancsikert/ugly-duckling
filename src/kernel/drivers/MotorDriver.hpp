#pragma once

#include <kernel/drivers/CurrentSenseDriver.hpp>

namespace farmhub::kernel::drivers {

enum class MotorPhase {
    FORWARD = 1,
    REVERSE = -1
};

MotorPhase operator-(MotorPhase phase) {
    return phase == MotorPhase::FORWARD
        ? MotorPhase::REVERSE
        : MotorPhase::FORWARD;
}

class PwmMotorDriver {
public:
    void stop() {
        drive(MotorPhase::FORWARD, 0);
    };

    virtual void drive(MotorPhase phase, double duty = 1) = 0;
};

class CurrentSensingMotorDriver
    : public PwmMotorDriver,
      public CurrentSenseDriver {
};

class ExternalCurrentSensingMotorDriver
    : public CurrentSensingMotorDriver {
public:
    ExternalCurrentSensingMotorDriver(PwmMotorDriver& motor, CurrentSenseDriver& currentSense)
        : motor(motor)
        , currentSense(currentSense) {
    }

    void drive(MotorPhase phase, double duty = 1) override {
        motor.drive(phase, duty);
    }

    double readCurrent() override {
        return currentSense.readCurrent();
    }

private:
    PwmMotorDriver& motor;
    CurrentSenseDriver& currentSense;
};

}    // namespace farmhub::kernel::drivers
