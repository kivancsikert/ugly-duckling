#pragma once

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

}    // namespace farmhub::kernel::drivers
