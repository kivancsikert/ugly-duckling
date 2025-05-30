#pragma once

#include <functional>
#include <list>

#include <Task.hpp>

namespace farmhub::kernel {

class ShutdownManager {
public:
    void registerShutdownListener(const std::function<void()>& listener) {
        shutdownListeners.push_back(listener);
    }

    void startShutdown() {
        // Run in separate task to allocate enough stack
        Task::run("shutdown", 8192, [this](Task& /*task*/) {
            // Notify all shutdown listeners
            for (auto& listener : shutdownListeners) {
                listener();
            }
            printf("Shutdown process finished\n");
        });
    }

private:
    std::list<std::function<void()>> shutdownListeners;
};

}    // namespace farmhub::kernel
