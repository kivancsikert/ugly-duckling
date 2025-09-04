#pragma once

#include <cstdint>

namespace farmhub::kernel::drivers {

enum class MotorPhase : int8_t {
    Forward = 1,
    Reverse = -1
};

MotorPhase operator-(MotorPhase phase);

class PwmMotorDriver {
public:
    virtual ~PwmMotorDriver() = default;

    void stop() {
        drive(MotorPhase::Forward, 0);
    };

    virtual void drive(MotorPhase phase, double duty) = 0;
};

}    // namespace farmhub::kernel::drivers
