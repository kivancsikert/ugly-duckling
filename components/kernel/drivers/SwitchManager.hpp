#pragma once

#include <chrono>
#include <functional>

#include <driver/gpio.h>
#include <hal/gpio_types.h>

#include <Concurrent.hpp>
#include <Pin.hpp>
#include <Task.hpp>
#include <utility>

using namespace std::chrono;

using farmhub::kernel::PinPtr;

namespace farmhub::kernel::drivers {

enum class SwitchMode : uint8_t {
    PullUp,
    PullDown
};

class Switch {
public:
    virtual ~Switch() = default;

    virtual const std::string& getName() const = 0;
    virtual InternalPinPtr getPin() const = 0;
    virtual bool isEngaged() const = 0;
};

struct SwitchStateChange {
    gpio_num_t gpio;
    bool engaged;
};

static void handleSwitchInterrupt(void* arg);

class SwitchManager final {
public:
    explicit SwitchManager(milliseconds debounceTime = 50ms) {
        Task::loop("switch-manager", 3072, [this, debounceTime](Task& /*task*/) {
            SwitchStateChange stateChange = switchStateInterrupts.take();
            std::shared_ptr<SwitchState> state;
            {
                Lock lock(switchStatesMutex);
                auto it = switchStates.find(stateChange.gpio);
                if (it == switchStates.end()) {
                    LOGE("Switch state change for unknown GPIO %d", stateChange.gpio);
                    return;
                }
                state = it->second;
            }

            // Software debounce: ignore state changes that happen too quickly
            auto now = system_clock::now();
            auto timeSinceLastChange = duration_cast<milliseconds>(now - state->lastChangeTime);
            if (timeSinceLastChange < debounceTime) {
                LOGV("Switch %s: debouncing, ignoring change (time since last: %lld ms)",
                    state->name.c_str(), timeSinceLastChange.count());
                return;
            }
            state->lastChangeTime = now;

            auto engaged = stateChange.engaged;
            LOGD("Switch %s is %s",
                state->name.c_str(), engaged ? "engaged" : "released");
            if (engaged) {
                state->engagementStarted = now;
                state->engagementHandler(state);
            } else if (state->engagementStarted.time_since_epoch().count() > 0) {
                auto duration = duration_cast<milliseconds>(now - state->engagementStarted);
                state->releaseHandler(state, duration);
            }
        });
    }

    using SwitchEngagementHandler = std::function<void(const std::shared_ptr<Switch>&)>;
    using SwitchReleaseHandler = std::function<void(const std::shared_ptr<Switch>&, milliseconds)>;

    std::shared_ptr<Switch> onEngaged(const std::string& name, const InternalPinPtr& pin, SwitchMode mode, SwitchEngagementHandler engagementHandler) {
        return registerHandler(
            name, pin, mode, std::move(engagementHandler), [](const std::shared_ptr<Switch>&, milliseconds) { });
    }

    std::shared_ptr<Switch> onReleased(const std::string& name, const InternalPinPtr& pin, SwitchMode mode, SwitchReleaseHandler releaseHandler) {
        return registerHandler(
            name, pin, mode, [](const std::shared_ptr<Switch>&) { }, std::move(releaseHandler));
    }

    std::shared_ptr<Switch> registerHandler(const std::string& name, const InternalPinPtr& pin, SwitchMode mode, SwitchEngagementHandler engagementHandler, SwitchReleaseHandler releaseHandler) {
        LOGI("Registering switch %s on pin %s, mode %s",
            name.c_str(), pin->getName().c_str(), mode == SwitchMode::PullUp ? "pull-up" : "pull-down");

        // Configure PIN_INPUT as input
        pin->pinMode(mode == SwitchMode::PullUp ? Pin::Mode::InputPullUp : Pin::Mode::InputPullDown);
        // gpio_set_direction(pin, GPIO_MODE_INPUT);
        // gpio_set_pull_mode(pin, mode == SwitchMode::PullUp ? GPIO_PULLUP_ONLY : GPIO_PULLDOWN_ONLY);

        auto switchState = std::make_shared<SwitchState>(name, pin, mode, this, std::move(engagementHandler), std::move(releaseHandler));
        {
            Lock lock(switchStatesMutex);
            switchStates.emplace(pin->getGpio(), switchState);
        }

        // Install GPIO ISR
        ESP_ERROR_THROW(gpio_set_intr_type(pin->getGpio(), GPIO_INTR_ANYEDGE));
        ESP_ERROR_THROW(gpio_isr_handler_add(pin->getGpio(), handleSwitchInterrupt, switchState.get()));

        return switchState;
    }

private:
    struct SwitchState final : public Switch {
    public:
        SwitchState(const std::string& name, const InternalPinPtr& pin, SwitchMode mode, SwitchManager* manager,
            SwitchEngagementHandler engagementHandler, SwitchReleaseHandler releaseHandler)
            : name(name)
            , pin(pin)
            , mode(mode)
            , manager(manager)
            , engagementHandler(std::move(engagementHandler))
            , releaseHandler(std::move(releaseHandler)) {
            // Initialize engagementStarted to an invalid time point
            engagementStarted = system_clock::time_point();
        }

        const std::string& getName() const override {
            return name;
        }

        InternalPinPtr getPin() const override {
            return pin;
        }

        bool isEngaged() const override {
            return pin->digitalRead() == (mode == SwitchMode::PullUp ? 0 : 1);
        }

    private:
        std::string name;
        InternalPinPtr pin;
        SwitchMode mode;
        SwitchManager* manager;

        SwitchEngagementHandler engagementHandler;
        SwitchReleaseHandler releaseHandler;

        time_point<system_clock> engagementStarted;
        time_point<system_clock> lastChangeTime;

        friend class SwitchManager;
        friend void handleSwitchInterrupt(void* arg);
    };

    Mutex switchStatesMutex;
    std::unordered_map<gpio_num_t, std::shared_ptr<SwitchState>> switchStates;

    CopyQueue<SwitchStateChange> switchStateInterrupts { "switchState-state-interrupts", 1 };
    friend void handleSwitchInterrupt(void* arg);
};

// ISR handler for GPIO interrupt
static void IRAM_ATTR handleSwitchInterrupt(void* arg) {
    auto* state = static_cast<SwitchManager::SwitchState*>(arg);
    // Must use gpio_get_level() to read the pin state instead of pin->digitalRead()
    // because we cannot call virtual methods from an ISR
    auto gpio = state->pin->getGpio();
    bool engaged = gpio_get_level(gpio) == (state->mode == SwitchMode::PullUp ? 0 : 1);

    // Use overwriteFromISR to ensure we never lose the latest state change
    // even if the queue is full. This is critical for limit switches.
    state->manager->switchStateInterrupts.overwriteFromISR(SwitchStateChange {
        .gpio = gpio,
        .engaged = engaged,
    });
}

}    // namespace farmhub::kernel::drivers
