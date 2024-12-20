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
    Watchdog(const std::string& name, const microseconds timeout, bool startImmediately, WatchdogCallback callback)
        : name(name)
        , timeout(timeout)
        , callback(callback) {
        esp_timer_create_args_t config = {
            .callback = [](void* arg) {
                auto watchdog = (Watchdog*) arg;
                watchdog->callback(WatchdogState::TimedOut);
            },
            .arg = this,
            .name = this->name.c_str(),
        };
        esp_err_t ret = esp_timer_create(&config, &timer);
        if (ret != ESP_OK) {
            LOGE("Failed to create watchdog timer: %s", esp_err_to_name(ret));
            esp_system_abort("Failed to create watchdog timer");
        }
        if (startImmediately) {
            restart();
        }
    }

    ~Watchdog() {
        cancel();
        esp_timer_delete(timer);
    }

    bool restart() {
        // TODO Add proper error handling
        if (esp_timer_restart(timer, timeout.count()) == ESP_ERR_INVALID_STATE) {
            esp_timer_start_once(timer, timeout.count());
        }
        callback(WatchdogState::Started);
        return true;
    }

    bool cancel() {
        // TODO Add proper error handling
        return esp_timer_stop(timer) == ESP_OK;
    }

private:
    const std::string name;
    const microseconds timeout;
    const WatchdogCallback callback;
    esp_timer_handle_t timer;
};

}    // namespace farmhub::kernel
