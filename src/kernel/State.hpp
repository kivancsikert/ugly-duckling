#pragma once

#include <chrono>
#include <list>

#include <freertos/FreeRTOS.h>
#include <freertos/event_groups.h>

#include <Arduino.h>

#include <ArduinoLog.h>

#include <kernel/Task.hpp>

using namespace std::chrono;

namespace farmhub::kernel {

// 0th bit reserved to indicate that a state has changed
static const int STATE_CHANGE_BIT_MASK = (1 << 0);

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

    bool IRAM_ATTR setFromISR() const {
        return hasAllBits(setBitsFromISR(eventBits | STATE_CHANGE_BIT_MASK));
    }

    bool clear() const {
        bool cleared = !hasAllBits(xEventGroupClearBits(eventGroup, eventBits));
        setBits(STATE_CHANGE_BIT_MASK);
        return cleared;
    }

    bool IRAM_ATTR clearFromISR() const {
        bool cleared = hasAllBits(xEventGroupClearBitsFromISR(eventGroup, eventBits));
        setBitsFromISR(STATE_CHANGE_BIT_MASK);
        return cleared;
    }

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
        Log.traceln("Creating state: %s",
            name.c_str());
        if (nextEventBit > 31) {
            throw std::runtime_error("Too many states");
        }
        EventBits_t eventBits = 1 << nextEventBit++;
        return StateSource(name, eventGroup, eventBits);
    }

    State combineStates(const String& name, const std::list<State>& states) const {
        Log.traceln("Creating combined state: %s",
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
