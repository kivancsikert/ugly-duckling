#include "MotorDriver.hpp"

namespace farmhub::kernel::drivers {

MotorPhase operator-(MotorPhase phase) {
    return phase == MotorPhase::FORWARD
        ? MotorPhase::REVERSE
        : MotorPhase::FORWARD;
}

}    // namespace farmhub::kernel::drivers
