#pragma once

#include <functional>

#include <kernel/Concurrent.hpp>
#include <kernel/Task.hpp>
#include <kernel/Time.hpp>

namespace farmhub::kernel {

typedef std::function<void()> WatchdogCallback;

class Watchdog {
public:
    Watchdog(const String& name, const ticks timeout, WatchdogCallback callback)
        : name(name)
        , timeout(timeout)
        , callback(callback) {
    }

    void restart() {
        Lock lock(updateMutex);
        abort();
        handle = Task::run(name, 2560, [this](Task& task) {
            task.delayUntil(timeout);
            Lock lock(updateMutex);
            callback();
        });
        Log.traceln("Watchdog started with a timeout of %F seconds",
            duration_cast<milliseconds>(timeout).count() / 1000);
    }

    void abort() {
        Lock lock(updateMutex);
        if (handle.isValid()) {
            handle.abort();
            handle = TaskHandle();
            Log.traceln("Watchdog aborted");
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
