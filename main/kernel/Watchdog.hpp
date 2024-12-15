#pragma once

#include <functional>

#include <kernel/Concurrent.hpp>
#include <kernel/Task.hpp>
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
        : name(name)
        , timeout(timeout)
        , callback(callback) {
        if (startImmediately) {
            restart();
        }
    }

    void restart() {
        Lock lock(updateMutex);
        cancel();
        handle = Task::run(name, 3172, [this](Task& task) {
            task.delayUntil(timeout);
            Lock lock(updateMutex);
            callback(WatchdogState::TimedOut);
        });
        callback(WatchdogState::Started);
        LOGD("Watchdog started with a timeout of %.2f seconds",
            duration_cast<milliseconds>(timeout).count() / 1000.0);
    }

    void cancel() {
        Lock lock(updateMutex);
        if (handle.isValid()) {
            handle.abort();
            callback(WatchdogState::Cancelled);
            handle = TaskHandle();
            LOGD("Watchdog cancelled");
        }
    }

private:
    const String name;
    const ticks timeout;
    const WatchdogCallback callback;

    RecursiveMutex updateMutex;
    TaskHandle handle;
};

}    // namespace farmhub::kernel
