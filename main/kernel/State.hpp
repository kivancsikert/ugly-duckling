#pragma once

#include <chrono>
#include <list>

#include <freertos/FreeRTOS.h>
#include <freertos/event_groups.h>

#include <Arduino.h>

#include <kernel/Time.hpp>

using namespace std::chrono;

namespace farmhub::kernel {

// 0th bit reserved to indicate that a state has changed
static constexpr int STATE_CHANGE_BIT_MASK = (1 << 0);

class StateManager;

/**
 * @brief Represents an observable state. Tasks can check the current state, or wait for the state to be set.
 *
 * Note that state can also be cleared, but this class does not allow waiting for that.
 */
class State {
public:
    State(const String& name, EventGroupHandle_t eventGroup, EventBits_t eventBits)
        : name(name)
        , eventGroup(eventGroup)
        , eventBits(eventBits) {
    }

    State(const State& event)
        : name(event.name)
        , eventGroup(event.eventGroup)
        , eventBits(event.eventBits) {
    }

    /**
     * @brief Checks if the state is set.
     */
    bool inline isSet() const {
        return awaitSet(ticks::zero());
    }

    /**
     * @brief Waits for the state to be set, or until timeout is elapsed.
     *
     * @return Whether the state was set before the timeout elapsed.
     */
    bool awaitSet(const ticks timeout) const {
        return hasAllBits(xEventGroupWaitBits(eventGroup, eventBits, false, true, timeout.count()));
    }

    /**
     * @brief Waits indefinitely for the state to be set.
     */
    void awaitSet() const {
        while (!awaitSet(ticks::max())) { }
    }

protected:
    bool constexpr hasAllBits(const EventBits_t bits) const {
        return (bits & eventBits) == eventBits;
    }

    const String name;
    const EventGroupHandle_t eventGroup;
    const EventBits_t eventBits;

    friend StateManager;
};

class StateSource
    : public State {
public:
    StateSource(const String& name, EventGroupHandle_t eventGroup, EventBits_t eventBits)
        : State(name, eventGroup, eventBits) {
    }

    StateSource(const StateSource& eventSource)
        : State(eventSource) {
    }

    bool set() const {
        return hasAllBits(setBits(eventBits | STATE_CHANGE_BIT_MASK));
    }

    bool IRAM_ATTR setFromISR() const;

    bool clear() const {
        bool cleared = !hasAllBits(xEventGroupClearBits(eventGroup, eventBits));
        setBits(STATE_CHANGE_BIT_MASK);
        return cleared;
    }

    bool IRAM_ATTR clearFromISR() const;

private:
    EventBits_t inline setBits(EventBits_t bits) const {
        return xEventGroupSetBits(eventGroup, bits);
    }

    EventBits_t inline setBitsFromISR(EventBits_t bits) const {
        BaseType_t xHigherPriorityTaskWoken = pdFALSE;
        auto result = xEventGroupSetBitsFromISR(eventGroup, bits, &xHigherPriorityTaskWoken);
        portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
        return result;
    }
};

}    // namespace farmhub::kernel
