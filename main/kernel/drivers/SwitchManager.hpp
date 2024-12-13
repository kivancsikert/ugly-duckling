#pragma once

#include <chrono>
#include <functional>

#include <driver/gpio.h>

#include <kernel/Concurrent.hpp>
#include <kernel/Pin.hpp>
#include <kernel/Task.hpp>

using namespace std::chrono;

using farmhub::kernel::PinPtr;

namespace farmhub::kernel::drivers {

enum class SwitchMode {
    PullUp,
    PullDown
};

class Switch {
public:
    virtual const std::string& getName() const = 0;
    virtual InternalPinPtr getPin() const = 0;
    virtual bool isEngaged() const = 0;
};

static void handleSwitchInterrupt(void* arg);

class SwitchManager {
public:
    SwitchManager() {
        Task::loop("switch-manager", 3072, [this](Task& task) {
            SwitchStateChange stateChange = switchStateInterrupts.take();
            auto state = stateChange.switchState;
            auto engaged = stateChange.engaged;
            LOGV("Switch %s is %s",
                state->name.c_str(), engaged ? "engaged" : "released");
            if (engaged) {
                state->engagementStarted = system_clock::now();
                state->engagementHandler(*state);
            } else if (state->engagementStarted.time_since_epoch().count() > 0) {
                auto duration = duration_cast<milliseconds>(system_clock::now() - state->engagementStarted);
                state->releaseHandler(*state, duration);
            }
        });
    }

    typedef std::function<void(const Switch&)> SwitchEngagementHandler;
    typedef std::function<void(const Switch&, milliseconds duration)> SwitchReleaseHandler;

    const Switch& onEngaged(const std::string& name, InternalPinPtr pin, SwitchMode mode, SwitchEngagementHandler engagementHandler) {
        return registerHandler(
            name, pin, mode, engagementHandler, [](const Switch&, milliseconds) {});
    }

    const Switch& onReleased(const std::string& name, InternalPinPtr pin, SwitchMode mode, SwitchReleaseHandler releaseHandler) {
        return registerHandler(
            name, pin, mode, [](const Switch&) {}, releaseHandler);
    }

    const Switch& registerHandler(const std::string& name, InternalPinPtr pin, SwitchMode mode, SwitchEngagementHandler engagementHandler, SwitchReleaseHandler releaseHandler) {
        LOGI("Registering switch %s on pin %s, mode %s",
            name.c_str(), pin->getName().c_str(), mode == SwitchMode::PullUp ? "pull-up" : "pull-down");

        // Configure PIN_INPUT as input
        pin->pinMode(mode == SwitchMode::PullUp ? Pin::Mode::InputPullUp : Pin::Mode::InputPullDown);
        // gpio_set_direction(pin, GPIO_MODE_INPUT);
        // gpio_set_pull_mode(pin, mode == SwitchMode::PullUp ? GPIO_PULLUP_ONLY : GPIO_PULLDOWN_ONLY);

        SwitchState* switchState = new SwitchState();
        switchState->name = name;
        switchState->pin = pin;
        switchState->mode = mode;
        switchState->manager = this;
        switchState->engagementHandler = engagementHandler;
        switchState->releaseHandler = releaseHandler;

        // Install GPIO ISR
        gpio_install_isr_service(0);
        gpio_isr_handler_add(pin->getGpio(), handleSwitchInterrupt, switchState);
        gpio_set_intr_type(pin->getGpio(), GPIO_INTR_ANYEDGE);

        return *switchState;
    }

private:
    struct SwitchState : public Switch {
    public:
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
    bool engaged = state->pin->digitalRead() == (state->mode == SwitchMode::PullUp ? 0 : 1);
    state->manager->queueSwitchStateChange(state, engaged);
}

}    // namespace farmhub::kernel::drivers
