#pragma once

#include <chrono>
#include <functional>
#include <map>

#include <driver/gpio.h>

#include <Arduino.h>

#include <kernel/Concurrent.hpp>
#include <kernel/Log.hpp>
#include <kernel/Task.hpp>

using namespace std::chrono;

namespace farmhub::kernel::drivers {

enum class SwitchMode {
    PullUp,
    PullDown
};

class Switch {
public:
    virtual const String& getName() const = 0;
    virtual gpio_num_t getPin() const = 0;
    virtual bool isEngaged() const = 0;
};

static void handleSwitchInterrupt(void* arg);

class SwitchManager {
public:
    SwitchManager() {
        Task::loop("switch-manager", 2560, [this](Task& task) {
            SwitchStateChange stateChange = switchStateInterrupts.take();
            auto state = stateChange.switchState;
            auto engaged = stateChange.engaged;
            Log.trace("Switch %s is %s",
                state->name.c_str(), engaged ? "engaged" : "released");
            if (engaged) {
                state->engagementStarted = system_clock::now();
                state->engagementHandler(*state);
            } else {
                auto duration = duration_cast<milliseconds>(system_clock::now() - state->engagementStarted);
                state->releaseHandler(*state, duration);
            }
        });
    }

    typedef std::function<void(const Switch&)> SwitchEngagementHandler;
    typedef std::function<void(const Switch&, milliseconds duration)> SwitchReleaseHandler;

    const Switch& onEngaged(const String& name, gpio_num_t pin, SwitchMode mode, SwitchEngagementHandler engagementHandler) {
        return registerHandler(
            name, pin, mode, engagementHandler, [](const Switch&, milliseconds) {});
    }

    const Switch& onReleased(const String& name, gpio_num_t pin, SwitchMode mode, SwitchReleaseHandler releaseHandler) {
        return registerHandler(
            name, pin, mode, [](const Switch&) {}, releaseHandler);
    }

    const Switch& registerHandler(const String& name, gpio_num_t pin, SwitchMode mode, SwitchEngagementHandler engagementHandler, SwitchReleaseHandler releaseHandler) {
        Log.info("Registering switch %s on pin %d, mode %s",
            name.c_str(), pin, mode == SwitchMode::PullUp ? "pull-up" : "pull-down");

        // Configure PIN_INPUT as input
        gpio_pad_select_gpio(pin);
        gpio_set_direction(pin, GPIO_MODE_INPUT);
        gpio_set_pull_mode(pin, mode == SwitchMode::PullUp ? GPIO_PULLUP_ONLY : GPIO_PULLDOWN_ONLY);

        SwitchState* switchState = new SwitchState();
        switchState->name = name;
        switchState->pin = pin;
        switchState->mode = mode;
        switchState->manager = this;
        switchState->engagementHandler = engagementHandler;
        switchState->releaseHandler = releaseHandler;

        // Install GPIO ISR
        gpio_install_isr_service(0);
        gpio_isr_handler_add(pin, handleSwitchInterrupt, switchState);
        gpio_set_intr_type(pin, GPIO_INTR_ANYEDGE);

        return *switchState;
    }

private:
    struct SwitchState : public Switch {
    public:
        const String& getName() const override {
            return name;
        }

        gpio_num_t getPin() const override {
            return pin;
        }

        bool isEngaged() const override {
            return digitalRead(pin) == (mode == SwitchMode::PullUp ? LOW : HIGH);
        }

    private:
        String name;
        gpio_num_t pin;
        SwitchMode mode;

        SwitchEngagementHandler engagementHandler;
        SwitchReleaseHandler releaseHandler;

        time_point<system_clock> engagementStarted;
        SwitchManager* manager;

        friend class SwitchManager;
        friend void handleSwitchInterrupt(void* arg);
    };

    struct SwitchStateChange {
        SwitchState* switchState;
        bool engaged;
    };

    void inline queueSwitchStateChange(SwitchState* state, bool engaged) {
        switchStateInterrupts.offerFromISR(SwitchStateChange { state, engaged });
    }

    CopyQueue<SwitchStateChange> switchStateInterrupts { "switchState-state-interrupts", 4 };
    friend void handleSwitchInterrupt(void* arg);
};

// ISR handler for GPIO interrupt
static void IRAM_ATTR handleSwitchInterrupt(void* arg) {
    SwitchManager::SwitchState* state = static_cast<SwitchManager::SwitchState*>(arg);
    bool engaged = digitalRead(state->pin) == (state->mode == SwitchMode::PullUp ? LOW : HIGH);
    state->manager->queueSwitchStateChange(state, engaged);
}

}    // namespace farmhub::kernel::drivers
