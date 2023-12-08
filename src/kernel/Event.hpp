#pragma once

#include <chrono>
#include <list>

#include <freertos/FreeRTOS.h>
#include <freertos/event_groups.h>

using namespace std::chrono;

namespace farmhub { namespace kernel {

// 0th bit reserved to indicate that an event has happened
static const int EVENT_BIT_MASK_EVENT_FIRED = (1 << 0);

class Event {
public:
    Event(const String& name, EventGroupHandle_t eventGroup, int eventBit)
        : name(name)
        , eventGroup(eventGroup)
        , eventBit(eventBit) {
    }

    Event(const Event& event)
        : name(event.name)
        , eventGroup(event.eventGroup)
        , eventBit(event.eventBit) {
    }

    bool inline isSet() {
        return await(0);
    }

    void await(milliseconds msToWait) {
        await(pdMS_TO_TICKS(msToWait.count()));
    }

    bool await(int ticksToWait = portMAX_DELAY) {
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

class EventSource
    : public Event {
public:
    EventSource(const String& name, EventGroupHandle_t eventGroup, int eventBit)
        : Event(name, eventGroup, eventBit) {
    }

    EventSource(const EventSource& eventSource)
        : Event(eventSource) {
    }

    bool emit() {
        return hasBits(setBits(asEventBits() | EVENT_BIT_MASK_EVENT_FIRED));
    }

    bool emitFromISR() {
        return hasBits(setBitsFromISR(asEventBits() | EVENT_BIT_MASK_EVENT_FIRED));
    }

    bool clear() {
        bool cleared = !hasBits(xEventGroupClearBits(eventGroup, asEventBits()));
        setBits(EVENT_BIT_MASK_EVENT_FIRED);
        return cleared;
    }

    bool clearFromISR() {
        bool cleared = hasBits(xEventGroupClearBitsFromISR(eventGroup, asEventBits()));
        setBitsFromISR(EVENT_BIT_MASK_EVENT_FIRED);
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

class EventGroup {
public:
    EventGroup()
        : eventGroup(xEventGroupCreate()) {
    }

    EventSource createEventSource(const String& name) {
        Serial.println("Creating event source " + name);
        if (nextEventBit > 31) {
            throw std::runtime_error("Too many events");
        }
        return EventSource(name, eventGroup, nextEventBit++);
    }

    bool waitForNextEvent(milliseconds msToWait) {
        return waitForNextEvent(pdMS_TO_TICKS(msToWait.count()));
    }

    bool waitForNextEvent(TickType_t ticksToWait = portMAX_DELAY) {
        // Since this is bit 0, we can just return the result directly
        bool receivedEvent = xEventGroupWaitBits(eventGroup, EVENT_BIT_MASK_EVENT_FIRED, false, true, ticksToWait);
        xEventGroupClearBits(eventGroup, EVENT_BIT_MASK_EVENT_FIRED);
        return receivedEvent;
    }

private:
    const EventGroupHandle_t eventGroup;
    int nextEventBit = 1;
};

}}    // namespace farmhub::kernel
