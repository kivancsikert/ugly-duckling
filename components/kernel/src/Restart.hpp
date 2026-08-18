#pragma once

#include <Task.hpp>

#include <esp_system.h>

#include <cstdio>

namespace cornucopia::ugly_duckling::kernel {

/**
 * @brief Flushes stdout, delays briefly to let MQTT messages reach the broker, then restarts.
 *
 * All application-level restarts should go through this function so the flush-and-delay logic
 * lives in one place — when we eventually replace the fixed 5s delay with something smarter
 * (e.g. waiting for the MQTT outbox to drain), only this function needs to change.
 */
[[noreturn]] inline void delayedRestart() {
    (void) fflush(stdout);
    fsync(fileno(stdout));
    Task::delay(5s);
    esp_restart();
    __builtin_unreachable();
}

}    // namespace cornucopia::ugly_duckling::kernel
