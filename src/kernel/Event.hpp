#pragma once

#include <chrono>
#include <list>

#include <freertos/FreeRTOS.h>
#include <freertos/event_groups.h>

using namespace std::chrono;

namespace farmhub { namespace kernel {

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

private:
    const EventGroupHandle_t eventGroup;
    int nextEventBit = 0;
    QueueHandle_t eventNotificationQueue = xQueueCreate(1, sizeof(EventBits_t));
};

}}    // namespace farmhub::kernel
