#pragma once

#include <functional>
#include <memory>
#include <mutex>

#include <Log.hpp>
#include <Pin.hpp>

namespace cornucopia::ugly_duckling::kernel::drivers {

/**
 * @brief Manages a shared binary output with multiple independent clients.
 *
 * The output is active when at least one client has acquired it, and
 * inactive when every client has released. Each client holds a Handle
 * obtained via createHandle().
 */
class SharedEnable : public std::enable_shared_from_this<SharedEnable> {
public:
    class Handle {
    public:
        Handle() = default;

        Handle(Handle&& other) noexcept
            : parent(std::move(other.parent))
            , acquired(other.acquired) {
            other.acquired = false;
        }

        Handle& operator=(Handle&& other) noexcept {
            if (this != &other) {
                release();
                parent = std::move(other.parent);
                acquired = other.acquired;
                other.acquired = false;
            }
            return *this;
        }

        ~Handle() {
            release();
        }

        /**
         * @brief Mark this handle as active; idempotent.
         */
        void acquire() {
            if (!acquired && parent) {
                acquired = true;
                parent->handleAcquired();
            }
        }

        /**
         * @brief Mark this handle as inactive; idempotent.
         */
        void release() {
            if (acquired && parent) {
                acquired = false;
                parent->handleReleased();
            }
        }

        bool isAcquired() const {
            return acquired;
        }

    private:
        friend class SharedEnable;

        explicit Handle(std::shared_ptr<SharedEnable> parent)
            : parent(std::move(parent)) {
        }

        std::shared_ptr<SharedEnable> parent;
        bool acquired = false;
    };

    using Actuator = std::function<void(bool)>;

    explicit SharedEnable(Actuator actuate)
        : actuate(std::move(actuate)) {
    }

    /**
     * @brief Create a SharedEnable that does nothing on state changes.
     */
    static std::shared_ptr<SharedEnable> noOp() {
        return std::make_shared<SharedEnable>([](bool) {});
    }

    /**
     * @brief Create a SharedEnable that drives a pin HIGH when active, LOW when inactive.
     */
    static std::shared_ptr<SharedEnable> forActiveHighPin(const PinPtr& pin) {
        pin->pinMode(Pin::Mode::Output);
        pin->digitalWrite(0);
        return std::make_shared<SharedEnable>([pin](bool active) {
            pin->digitalWrite(active ? 1 : 0);
        });
    }

    /**
     * @brief Create a SharedEnable that drives a pin LOW when active, HIGH when inactive.
     */
    static std::shared_ptr<SharedEnable> forActiveLowPin(const PinPtr& pin) {
        pin->pinMode(Pin::Mode::Output);
        pin->digitalWrite(1);
        return std::make_shared<SharedEnable>([pin](bool active) {
            pin->digitalWrite(active ? 0 : 1);
        });
    }

    Handle createHandle() {
        return Handle(shared_from_this());
    }

private:
    void handleAcquired() {
        std::lock_guard<std::mutex> lock(mutex);
        if (++activeCount == 1) {
            actuate(true);
        }
    }

    void handleReleased() {
        std::lock_guard<std::mutex> lock(mutex);
        if (--activeCount == 0) {
            actuate(false);
        }
    }

    Actuator actuate;
    int activeCount = 0;
    std::mutex mutex;
};

}    // namespace cornucopia::ugly_duckling::kernel::drivers
