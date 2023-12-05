#pragma once

#include <freertos/FreeRTOS.h>
#include <freertos/event_groups.h>

namespace farmhub { namespace kernel {

class EventSource {
public:
    EventSource(EventGroupHandle_t eventGroup, int eventBit)
        : eventGroup(eventGroup)
        , eventBit(eventBit) {
    }

    void await(int ticksToWait = portMAX_DELAY) {
        xEventGroupWaitBits(eventGroup, asEventBits(), false, true, ticksToWait);
    }

    EventBits_t inline asEventBits() {
        return 1 << eventBit;
    }

protected:
    void emitEvent() {
        xEventGroupSetBits(eventGroup, asEventBits());
    }

    void emitEventFromISR() {
        int xHigherPriorityTaskWoken = pdFALSE;
        xEventGroupSetBitsFromISR(eventGroup, asEventBits(), &xHigherPriorityTaskWoken);
        portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
    }

private:
    const EventGroupHandle_t eventGroup;
    const int eventBit;
};

}}    // namespace farmhub::kernel
