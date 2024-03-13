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

namespace farmhub::kernel {

enum class ButtonMode {
    PullUp,
    PullDown
};

class Button {
public:
    virtual const String& getName() = 0;
    virtual gpio_num_t getPin() = 0;
};

typedef std::function<void(const Button&)> ButtonPressHandler;
typedef std::function<void(const Button&, milliseconds duration)> ButtonReleaseHandler;

class ButtonManager;

struct ButtonState : public Button {
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
    ButtonMode mode;

    ButtonPressHandler pressHandler;
    ButtonReleaseHandler releaseHandler;

    time_point<system_clock> pressTime;

    friend class ButtonManager;
    friend void handleButtonInterrupt(void* arg);
};

struct ButtonStateChange {
    ButtonState* button;
    bool pressed;
};

static CopyQueue<ButtonStateChange> buttonStateInterrupts("button-state-interrupts", 4);

// ISR handler for GPIO interrupt
static void IRAM_ATTR handleButtonInterrupt(void* arg) {
    ButtonState* state = (ButtonState*) arg;
    bool pressed = digitalRead(state->pin) == (state->mode == ButtonMode::PullUp ? LOW : HIGH);
    buttonStateInterrupts.offerFromISR(ButtonStateChange { state, pressed });
}

class ButtonManager {
public:
    ButtonManager() {
        Task::loop("button-manager", 2560, [this](Task& task) {
            ButtonStateChange stateChange = buttonStateInterrupts.take();
            auto state = stateChange.button;
            auto pressed = stateChange.pressed;
            Log.verboseln("Button %s %s", state->pin,
                state->name.c_str(), pressed ? "pressed" : "released");
            if (pressed) {
                state->pressTime = system_clock::now();
                state->pressHandler(*state);
            } else {
                auto pressDuration = duration_cast<milliseconds>(system_clock::now() - state->pressTime);
                state->releaseHandler(*state, pressDuration);
            }
        });
    }

    void registerButtonPressHandler(const String& name, gpio_num_t pin, ButtonMode mode, ButtonPressHandler pressHandler) {
        registerButtonHandler(name, pin, mode, pressHandler, [](const Button&, milliseconds) {});
    }

    void registerButtonReleaseHandler(const String& name, gpio_num_t pin, ButtonMode mode, ButtonReleaseHandler releaseHandler) {
        registerButtonHandler(
            name, pin, mode, [](const Button&) {}, releaseHandler);
    }

    void registerButtonHandler(const String& name, gpio_num_t pin, ButtonMode mode, ButtonPressHandler pressHandler, ButtonReleaseHandler releaseHandler) {
        Log.infoln("Registering button %s on pin %d, mode %s",
            name.c_str(), pin, mode == ButtonMode::PullUp ? "pull-up" : "pull-down");

        // Configure PIN_INPUT as input
        gpio_pad_select_gpio(pin);
        gpio_set_direction(pin, GPIO_MODE_INPUT);
        gpio_set_pull_mode(pin, mode == ButtonMode::PullUp ? GPIO_PULLUP_ONLY : GPIO_PULLDOWN_ONLY);

        ButtonState* button = new ButtonState();
        button->name = name;
        button->pin = pin;
        button->mode = mode;
        button->pressHandler = pressHandler;
        button->releaseHandler = releaseHandler;

        // Install GPIO ISR
        gpio_install_isr_service(0);
        gpio_isr_handler_add(pin, handleButtonInterrupt, button);
        gpio_set_intr_type(pin, GPIO_INTR_ANYEDGE);
    }
};

}    // namespace farmhub::kernel
