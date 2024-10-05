#pragma once

#include <functional>

#include <kernel/Concurrent.hpp>
#include <kernel/Task.hpp>
#include <kernel/Time.hpp>

namespace farmhub::kernel {

enum class WatchdogState {
    Started,
    Cacnelled,
    TimedOut
};

typedef std::function<void(WatchdogState)> WatchdogCallback;

class Watchdog {
public:
    Watchdog(const String& name, const ticks timeout, WatchdogCallback callback)
        : name(name)
        , timeout(timeout)
        , callback(callback) {
    }

    void restart() {
        Lock lock(updateMutex);
        cancel();
        handle = Task::run(name, 2560, [this](Task& task) {
            task.delayUntil(timeout);
            Lock lock(updateMutex);
            callback(WatchdogState::TimedOut);
        });
        callback(WatchdogState::Started);
        Log.debug("Watchdog started with a timeout of %.2f seconds",
            duration_cast<milliseconds>(timeout).count() / 1000.0);
    }

    void cancel() {
        Lock lock(updateMutex);
        if (handle.isValid()) {
            handle.abort();
            callback(WatchdogState::Cacnelled);
            handle = TaskHandle();
            Log.debug("Watchdog cancelled");
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
