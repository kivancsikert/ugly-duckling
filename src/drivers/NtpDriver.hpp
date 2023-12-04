#pragma once

#include <time.h>

#include <Event.hpp>
#include <Task.hpp>

namespace farmhub { namespace device { namespace drivers {

class NtpDriver
    : Task,
      public EventEmitter {
public:
    NtpDriver(EventGroupHandle_t eventGroup, int eventBit)
        : Task("Ensure NTP sync")
        , EventEmitter(eventGroup, eventBit) {
    }

protected:
    void run() override {
        while (true) {
            time_t now;
            time(&now);
            if (now > (2022 - 1970) * 365 * 24 * 60 * 60) {
                Serial.println("Time configured, exiting task");
                emitEvent();
                break;
            }
            delayUntil(1000);
        }
    }
};

}}}    // namespace farmhub::device::drivers
