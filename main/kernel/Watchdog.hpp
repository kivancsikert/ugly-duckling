#pragma once

#include <functional>

#include <esp_check.h>

#include <kernel/Time.hpp>

namespace farmhub::kernel {

enum class WatchdogState {
    Started,
    Cancelled,
    TimedOut
};

typedef std::function<void(WatchdogState)> WatchdogCallback;

class Watchdog {
public:
    Watchdog(const String& name, const ticks timeout, bool startImmediately, WatchdogCallback callback)
        : timeout(timeout)
        , callback(callback) {
        timer = xTimerCreate(name.c_str(), timeout.count(), false, this, [](TimerHandle_t timer) {
            LOGD("Watchdog '%s' timed out", pcTimerGetName(timer));
            auto watchdog = static_cast<Watchdog*>(pvTimerGetTimerID(timer));
            watchdog->callback(WatchdogState::TimedOut);
        });
        if (!timer) {
            LOGE("Failed to create watchdog timer");
            esp_system_abort("Failed to create watchdog timer");
        }
        if (startImmediately) {
            restart();
        }
    }

    ~Watchdog() {
        xTimerDelete(timer, 0);
    }

    bool restart() {
        if (xTimerReset(timer, 0) != pdPASS) {
            LOGE("Failed to reset watchdog timer '%s'", pcTimerGetName(timer));
            return false;
        }
        callback(WatchdogState::Started);
        return true;
    }

    bool cancel() {
        if (xTimerStop(timer, 0) != pdPASS) {
            LOGE("Failed to stop watchdog timer '%s'", pcTimerGetName(timer));
            return false;
        }
        callback(WatchdogState::Cancelled);
        return true;
    }

private:
    const ticks timeout;
    const WatchdogCallback callback;

    TimerHandle_t timer = nullptr;
};

}    // namespace farmhub::kernel
