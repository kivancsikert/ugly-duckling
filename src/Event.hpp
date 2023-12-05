#pragma once

#include <freertos/FreeRTOS.h>
#include <freertos/event_groups.h>

namespace farmhub { namespace device {

class Events {
public:
    Events(EventGroupHandle_t eventGroup, EventBits_t eventBits, bool waitForAll = true)
        : eventGroup(eventGroup)
        , eventBits(eventBits)
        , waitForAll(waitForAll) {};

    void waitFor(bool clearOnExit = false, int ticksToWait = portMAX_DELAY) {
        xEventGroupWaitBits(eventGroup, eventBits, clearOnExit, waitForAll, ticksToWait);
    }

private:
    const EventGroupHandle_t eventGroup;
    const EventBits_t eventBits;
    const bool waitForAll;
};

class EventSource {
public:
    EventSource(EventGroupHandle_t eventGroup, int eventBit)
        : eventGroup(eventGroup)
        , eventBit(eventBit) {
    }

    void waitFor(bool clearOnExit = false, int ticksToWait = portMAX_DELAY) {
        xEventGroupWaitBits(eventGroup, asEventBits(), clearOnExit, true, ticksToWait);
    }

    EventBits_t inline asEventBits() {
        return 1 << eventBit;
    }

protected:
    void emitEvent() {
        xEventGroupSetBits(eventGroup, asEventBits());
    }

private:
    const EventGroupHandle_t eventGroup;
    const int eventBit;
};

}}    // namespace farmhub::device
