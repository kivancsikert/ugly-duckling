#pragma once

#include <chrono>
#include <functional>
#include <map>

#include <driver/gpio.h>

#include <Arduino.h>

#include <ArduinoLog.h>

#include <kernel/Concurrent.hpp>
#include <kernel/Task.hpp>

using namespace std::chrono;

namespace farmhub::kernel::drivers {

enum class SwitchMode {
    PullUp,
    PullDown
};

class Switch {
public:
    virtual const String& getName() = 0;
    virtual gpio_num_t getPin() = 0;
};

typedef std::function<void(const Switch&)> SwitchEngagementHandler;
typedef std::function<void(const Switch&, milliseconds duration)> SwitchReleaseHandler;

class SwitchManager;
static void handleSwitchInterrupt(void* arg);

struct SwitchState : public Switch {
public:
    const String& getName() override {
        return name;
    }

    gpio_num_t getPin() override {
        return pin;
    }

private:
    String name;
    gpio_num_t pin;
    SwitchMode mode;

    SwitchEngagementHandler engagementHandler;
    SwitchReleaseHandler releaseHandler;

    time_point<system_clock> engagementStarted;

    friend class SwitchManager;
    friend void handleSwitchInterrupt(void* arg);
};

struct SwitchStateChange {
    SwitchState* switchState;
    bool engaged;
};

static CopyQueue<SwitchStateChange> switchStateInterrupts("switchState-state-interrupts", 4);

// ISR handler for GPIO interrupt
static void IRAM_ATTR handleSwitchInterrupt(void* arg) {
    SwitchState* state = (SwitchState*) arg;
    bool engaged = digitalRead(state->pin) == (state->mode == SwitchMode::PullUp ? LOW : HIGH);
    switchStateInterrupts.offerFromISR(SwitchStateChange { state, engaged });
}

class SwitchManager {
public:
    SwitchManager() {
        Task::loop("switch-manager", 2560, [this](Task& task) {
            SwitchStateChange stateChange = switchStateInterrupts.take();
            auto state = stateChange.switchState;
            auto engaged = stateChange.engaged;
            Log.verboseln("Switch %s is %s",
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

    void registerSwitchEngagementHandler(const String& name, gpio_num_t pin, SwitchMode mode, SwitchEngagementHandler engagementHandler) {
        registerSwitchHandler(name, pin, mode, engagementHandler, [](const Switch&, milliseconds) {});
    }

    void registerSwitchReleaseHandler(const String& name, gpio_num_t pin, SwitchMode mode, SwitchReleaseHandler releaseHandler) {
        registerSwitchHandler(
            name, pin, mode, [](const Switch&) {}, releaseHandler);
    }

    void registerSwitchHandler(const String& name, gpio_num_t pin, SwitchMode mode, SwitchEngagementHandler engagementHandler, SwitchReleaseHandler releaseHandler) {
        Log.infoln("Registering switch %s on pin %d, mode %s",
            name.c_str(), pin, mode == SwitchMode::PullUp ? "pull-up" : "pull-down");

        // Configure PIN_INPUT as input
        gpio_pad_select_gpio(pin);
        gpio_set_direction(pin, GPIO_MODE_INPUT);
        gpio_set_pull_mode(pin, mode == SwitchMode::PullUp ? GPIO_PULLUP_ONLY : GPIO_PULLDOWN_ONLY);

        SwitchState* switchState = new SwitchState();
        switchState->name = name;
        switchState->pin = pin;
        switchState->mode = mode;
        switchState->engagementHandler = engagementHandler;
        switchState->releaseHandler = releaseHandler;

        // Install GPIO ISR
        gpio_install_isr_service(0);
        gpio_isr_handler_add(pin, handleSwitchInterrupt, switchState);
        gpio_set_intr_type(pin, GPIO_INTR_ANYEDGE);
    }
};

}    // namespace farmhub::kernel::drivers
