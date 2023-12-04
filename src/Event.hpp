#pragma once

#include <freertos/FreeRTOS.h>
#include <freertos/event_groups.h>

namespace farmhub { namespace device {

class EventEmitter {
public:
    EventEmitter(EventGroupHandle_t eventGroup, int eventBit)
        : eventGroup(eventGroup)
        , eventBit(eventBit) {
    }

    void emitEvent() {
        xEventGroupSetBits(eventGroup, asEventBits());
    }

    EventBits_t inline asEventBits() {
        return 1 << eventBit;
    }

private:
    const EventGroupHandle_t eventGroup;
    const int eventBit;
};

}}    // namespace farmhub::device
