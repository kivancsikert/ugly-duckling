#include "MotorDriver.hpp"

namespace farmhub::kernel::drivers {

MotorPhase operator-(MotorPhase phase) {
    return phase == MotorPhase::Forward
        ? MotorPhase::Reverse
        : MotorPhase::Forward;
}

}    // namespace farmhub::kernel::drivers
