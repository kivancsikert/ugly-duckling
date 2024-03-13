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

typedef std::function<void(gpio_num_t, milliseconds duration)> ButtonPressHandler;

struct ButtonState {
    String name;
    gpio_num_t pin;
    ButtonMode mode;

    ButtonPressHandler handler;

    time_point<system_clock> pressTime;
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
            } else {
                auto pressDuration = duration_cast<milliseconds>(system_clock::now() - state->pressTime);
                state->handler(state->pin, pressDuration);
            }
        });
    }

    void registerButtonPressHandler(const String& name, gpio_num_t pin, ButtonMode mode, ButtonPressHandler handler) {
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
        button->handler = handler;

        // Install GPIO ISR
        gpio_install_isr_service(0);
        gpio_isr_handler_add(pin, handleButtonInterrupt, button);
        gpio_set_intr_type(pin, GPIO_INTR_ANYEDGE);
    }
};

}    // namespace farmhub::kernel
