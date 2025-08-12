#include "MotorDriver.hpp"

namespace farmhub::kernel::drivers {

MotorPhase operator-(MotorPhase phase) {
    return phase == MotorPhase::FORWARD
        ? MotorPhase::REVERSE
        : MotorPhase::FORWARD;
}

}
