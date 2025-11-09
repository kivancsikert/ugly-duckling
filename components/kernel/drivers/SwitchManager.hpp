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

LOGGING_TAG(SWITCH, "switch")

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
    using SwitchEngagementHandler = std::function<void(const std::shared_ptr<Switch>&)>;
    using SwitchReleaseHandler = std::function<void(const std::shared_ptr<Switch>&, milliseconds)>;

    struct SwitchConfig {
        std::string name;
        InternalPinPtr pin;
        SwitchMode mode;
        SwitchEngagementHandler onEngaged = nullptr;
        SwitchReleaseHandler onReleased = nullptr;
        milliseconds debounceTime = 50ms;
    };

    SwitchManager() {
        Task::loop("switch-manager", 3072, [this](Task& /*task*/) {
            SwitchStateChange stateChange = switchStateInterrupts.take();
            std::shared_ptr<SwitchState> state;
            {
                Lock lock(switchStatesMutex);
                auto it = switchStates.find(stateChange.gpio);
                if (it == switchStates.end()) {
                    LOGTE(SWITCH, "Switch state change for unknown GPIO %d", stateChange.gpio);
                    return;
                }
                state = it->second;
            }

            // Software debounce: ignore state changes that happen too quickly
            auto now = system_clock::now();
            auto engaged = stateChange.engaged;
            auto timeSinceLastChange = duration_cast<milliseconds>(now - state->lastChangeTime);
            if (timeSinceLastChange < state->debounceTime) {
                LOGTV(SWITCH, "Switch %s: debouncing, ignoring state of '%s' (time since last: %lld ms)",
                    state->name.c_str(),
                    engaged ? "engaged" : "released",
                    timeSinceLastChange.count());
                return;
            }
            state->lastChangeTime = now;

            LOGTD(SWITCH, "Switch %s is %s",
                state->name.c_str(), engaged ? "engaged" : "released");
            if (engaged) {
                if (state->engagementHandler) {
                    state->engagementHandler(state);
                }
            } else {
                auto duration = duration_cast<milliseconds>(now - state->lastChangeTime);
                if (state->releaseHandler) {
                    state->releaseHandler(state, duration);
                }
            }
        });
    }

    std::shared_ptr<Switch> registerSwitch(const SwitchConfig& config) {
        LOGTI(SWITCH, "Registering switch %s on pin %s, mode %s, debounce %lld ms",
            config.name.c_str(), config.pin->getName().c_str(),
            config.mode == SwitchMode::PullUp ? "pull-up" : "pull-down",
            config.debounceTime.count());

        // Configure PIN_INPUT as input
        config.pin->pinMode(config.mode == SwitchMode::PullUp ? Pin::Mode::InputPullUp : Pin::Mode::InputPullDown);

        // Enable hardware glitch filter to remove very short pulses (~25ns)
        // config.pin->enableGlitchFilter();

        auto switchState = std::make_shared<SwitchState>(
            config.name,
            config.pin,
            config.mode,
            this,
            config.onEngaged,
            config.onReleased,
            config.debounceTime);
        {
            Lock lock(switchStatesMutex);
            switchStates.emplace(config.pin->getGpio(), switchState);
        }

        // Install GPIO ISR
        ESP_ERROR_THROW(gpio_set_intr_type(config.pin->getGpio(), GPIO_INTR_ANYEDGE));
        ESP_ERROR_THROW(gpio_isr_handler_add(config.pin->getGpio(), handleSwitchInterrupt, switchState.get()));

        return switchState;
    }

private:
    struct SwitchState final : public Switch {
    public:
        SwitchState(const std::string& name, const InternalPinPtr& pin, SwitchMode mode, SwitchManager* manager,
            SwitchEngagementHandler engagementHandler, SwitchReleaseHandler releaseHandler, milliseconds debounceTime)
            : name(name)
            , pin(pin)
            , mode(mode)
            , manager(manager)
            , engagementHandler(std::move(engagementHandler))
            , releaseHandler(std::move(releaseHandler))
            , debounceTime(debounceTime)
            , lastChangeTime(system_clock::now()) {
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

        milliseconds debounceTime;
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
