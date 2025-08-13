#include "State.hpp"
#include "freertos/idf_additions.h"

namespace farmhub::kernel {

bool StateSource::setFromISR() const {
    return hasAllBits(setBitsFromISR(eventBits | STATE_CHANGE_BIT_MASK));
}

bool StateSource::clearFromISR() const {
    bool cleared = hasAllBits(xEventGroupClearBitsFromISR(eventGroup, eventBits));
    setBitsFromISR(STATE_CHANGE_BIT_MASK);
    return cleared;
}

}    // namespace farmhub::kernel
