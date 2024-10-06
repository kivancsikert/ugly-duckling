#include "State.hpp"

namespace farmhub::kernel {

bool StateSource::setFromISR() const {
    return hasAllBits(setBitsFromISR(eventBits | STATE_CHANGE_BIT_MASK));
}

bool IRAM_ATTR StateSource::clearFromISR() const {
    bool cleared = hasAllBits(xEventGroupClearBitsFromISR(eventGroup, eventBits));
    setBitsFromISR(STATE_CHANGE_BIT_MASK);
    return cleared;
}

}    // namespace farmhub::kernel
