#pragma once

#include <time.h>

#include <Task.hpp>

namespace farmhub { namespace device { namespace drivers {

class NtpDriver : Task {
public:
    NtpDriver()
        : Task("Ensure NTP sync") {
    }

protected:
    void run() override {
        while (true) {
            time_t now;
            time(&now);
            if (now > (2022 - 1970) * 365 * 24 * 60 * 60) {
                Serial.println("Time configured, exiting task");
                break;
            }
            delayUntil(1000);
        }
    }
};

}}}    // namespace farmhub::device::drivers
