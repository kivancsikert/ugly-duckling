#pragma once

#include <chrono>
#include <list>

#include <Arduino.h>

#include <freertos/FreeRTOS.h>
#include <freertos/event_groups.h>

using namespace std::chrono;

namespace farmhub { namespace kernel {

// 0th bit reserved to indicate that a state has changed
static const int STATE_CHANGE_BIT_MASK = (1 << 0);

/**
 * @brief Represents an observable state. Tasks can check the current state, or wait for the state to be set.
 *
 * Note that state can also be cleared, but this class does not allow waiting for that.
 */
class State {
public:
    State(const String& name, EventGroupHandle_t eventGroup, int eventBit)
        : name(name)
        , eventGroup(eventGroup)
        , eventBit(eventBit) {
    }

    State(const State& event)
        : name(event.name)
        , eventGroup(event.eventGroup)
        , eventBit(event.eventBit) {
    }

    /**
     * @brief Checks if the state is set.
     */
    bool inline isSet() {
        return awaitSet(0);
    }

    /**
     * @brief Waits for the state to be set.
     *
     * @return Whether the state was set before the timeout elapsed.
     */
    bool awaitSet(milliseconds timeout) {
        return awaitSet(pdMS_TO_TICKS(timeout.count()));
    }

    /**
     * @brief Waits for the state to be set.
     *
     * @return Whether the state was set before the given ticks have elapsed.
     */
    bool awaitSet(int ticksToWait = portMAX_DELAY) {
        return hasBits(xEventGroupWaitBits(eventGroup, asEventBits(), false, true, ticksToWait));
    }

protected:
    EventBits_t inline asEventBits() {
        return 1 << eventBit;
    }

    bool hasBits(EventBits_t bits) {
        return (bits & asEventBits()) == asEventBits();
    }

    const String name;
    const EventGroupHandle_t eventGroup;
    const int eventBit;
};

class StateSource
    : public State {
public:
    StateSource(const String& name, EventGroupHandle_t eventGroup, int eventBit)
        : State(name, eventGroup, eventBit) {
    }

    StateSource(const StateSource& eventSource)
        : State(eventSource) {
    }

    bool set() {
        return hasBits(setBits(asEventBits() | STATE_CHANGE_BIT_MASK));
    }

    bool setFromISR() {
        return hasBits(setBitsFromISR(asEventBits() | STATE_CHANGE_BIT_MASK));
    }

    bool clear() {
        bool cleared = !hasBits(xEventGroupClearBits(eventGroup, asEventBits()));
        setBits(STATE_CHANGE_BIT_MASK);
        return cleared;
    }

    bool clearFromISR() {
        bool cleared = hasBits(xEventGroupClearBitsFromISR(eventGroup, asEventBits()));
        setBitsFromISR(STATE_CHANGE_BIT_MASK);
        return cleared;
    }

private:
    EventBits_t inline setBits(EventBits_t bits) {
        return xEventGroupSetBits(eventGroup, bits);
    }

    EventBits_t inline setBitsFromISR(EventBits_t bits) {
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
        Serial.println("Creating state: " + name);
        if (nextEventBit > 31) {
            throw std::runtime_error("Too many states");
        }
        return StateSource(name, eventGroup, nextEventBit++);
    }

    bool waitStateChange(milliseconds timeout) {
        return waitStateChange(pdMS_TO_TICKS(timeout.count()));
    }

    bool waitStateChange(TickType_t ticksToWait = portMAX_DELAY) {
        // Since this is bit 0, we can just return the result directly
        bool receivedEvent = xEventGroupWaitBits(eventGroup, STATE_CHANGE_BIT_MASK, false, true, ticksToWait);
        xEventGroupClearBits(eventGroup, STATE_CHANGE_BIT_MASK);
        return receivedEvent;
    }

private:
    const EventGroupHandle_t eventGroup;
    int nextEventBit = 1;
};

}}    // namespace farmhub::kernel
