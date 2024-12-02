#pragma once

#include <kernel/State.hpp>

namespace farmhub::kernel {

/**
 * @brief Handles a group of states and allows waiting for the next state to change.
 *
 * Note the state change triggers when one of the states is set or is cleared.
 * This is different from the State class, which only triggers when the state is set,
 * but not when it's cleared.
 */
class StateManager {
public:
    StateManager()
        : eventGroup(xEventGroupCreate()) {
    }

    StateSource createStateSource(const String& name) {
        Log.trace("Creating state: %s",
            name.c_str());
        if (nextEventBit > 31) {
            throw std::runtime_error("Too many states");
        }
        EventBits_t eventBits = 1 << nextEventBit++;
        return StateSource(name, eventGroup, eventBits);
    }

    State combineStates(const String& name, const std::list<State>& states) const {
        Log.debug("Creating combined state: %s",
            name.c_str());
        int eventBits = 0;
        for (auto& state : states) {
            eventBits |= state.eventBits;
        }
        return State(name, eventGroup, eventBits);
    }

    /**
     * @brief Wait indefinitely for any state to change.
     */
    void awaitStateChange() const {
        while (!awaitStateChange(ticks::max())) { }
    }

    /**
     * @brief Wait for any state to change, or for the timeout to elapse.
     *
     * @return Whether the state changed before the timeout elapsed.
     */
    bool awaitStateChange(ticks timeout) const {
        // Since this is bit 0, we can just return the result directly
        return xEventGroupWaitBits(eventGroup, STATE_CHANGE_BIT_MASK, true, true, timeout.count());
    }

private:
    const EventGroupHandle_t eventGroup;
    int nextEventBit = 1;
};

}    // namespace farmhub::kernel
