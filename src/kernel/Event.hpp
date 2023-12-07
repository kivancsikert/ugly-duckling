#pragma once

#include <chrono>

#include <freertos/FreeRTOS.h>
#include <freertos/event_groups.h>

using namespace std::chrono;

namespace farmhub { namespace kernel {

// TODO Separate Event (an observable) from EventSource (that emits the event).
class Event {
public:
    Event(EventGroupHandle_t eventGroup, int eventBit)
        : eventGroup(eventGroup)
        , eventBit(eventBit) {
    }

    void await(milliseconds msToWait) {
        await(pdMS_TO_TICKS(msToWait.count()));
    }

    bool await(int ticksToWait = portMAX_DELAY) {
        return hasBits(xEventGroupWaitBits(eventGroup, asEventBits(), false, true, ticksToWait));
    }

    bool emit() {
        return hasBits(xEventGroupSetBits(eventGroup, asEventBits()));
    }

    bool emitFromISR() {
        BaseType_t xHigherPriorityTaskWoken = pdFALSE;
        auto result = xEventGroupSetBitsFromISR(eventGroup, asEventBits(), &xHigherPriorityTaskWoken);
        portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
        return hasBits(result);
    }

    bool clear() {
        return hasBits(xEventGroupClearBits(eventGroup, asEventBits()));
    }

    bool clearFromISR() {
        return hasBits(xEventGroupClearBitsFromISR(eventGroup, asEventBits()));
    }

private:
    EventBits_t inline asEventBits() {
        return 1 << eventBit;
    }

    bool hasBits(EventBits_t bits) {
        return (bits & asEventBits()) == asEventBits();
    }

    const EventGroupHandle_t eventGroup;
    const int eventBit;
};

}}    // namespace farmhub::kernel
