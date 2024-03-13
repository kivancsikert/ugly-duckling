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

typedef std::function<void(gpio_num_t)> ButtonPressHandler;

struct ButtonState {
    gpio_num_t pin;
    ButtonMode mode;
    milliseconds pressTimeout;

    ButtonPressHandler handler;

    bool pressed = false;
    time_point<system_clock> lastPressTime;
    Mutex triggered;
    TaskHandle task = nullptr;
};

static InterrputQueue<ButtonState> buttonStateInterrupts("button-state-interrupts", 4);

// ISR handler for GPIO interrupt
static void IRAM_ATTR handleButtonInterrupt(void* arg) {
    ButtonState* state = (ButtonState*) arg;
    buttonStateInterrupts.offerFromISR(state);
}

class ButtonManager {
public:
    ButtonManager() {
        Task::loop("button-manager", 2560, [this](Task& task) {
            ButtonState& state = buttonStateInterrupts.take();
            auto pressed = digitalRead(state.pin) == (state.mode == ButtonMode::PullUp ? LOW : HIGH);
            Log.verboseln("Button %d %s", state.pin, pressed ? "pressed" : "released");
            if (pressed != state.pressed) {
                state.pressed = pressed;
                if (pressed) {
                    state.lastPressTime = system_clock::now();
                    state.task = Task::run("button-handler", [&state](Task& task) {
                        task.delay(state.pressTimeout);
                        Lock lock(state.triggered);
                        Log.verboseln("Button %d pressed for %d ms, triggering",
                            state.pin, duration_cast<milliseconds>(state.pressTimeout).count());
                        state.handler(state.pin);
                    });
                } else {
                    Lock lock(state.triggered);
                    state.task.abort();
                    state.task = nullptr;
                }
            }
        });
    }

    void registerButtonPressHandler(gpio_num_t pin, ButtonMode mode, milliseconds pressTimeout, ButtonPressHandler handler) {
        Log.infoln("Registering button on pin %d, mode %s",
            pin, mode == ButtonMode::PullUp ? "pull-up" : "pull-down");

        // Configure PIN_INPUT as input
        gpio_pad_select_gpio(pin);
        gpio_set_direction(pin, GPIO_MODE_INPUT);
        gpio_set_pull_mode(pin, mode == ButtonMode::PullUp ? GPIO_PULLUP_ONLY : GPIO_PULLDOWN_ONLY);

        ButtonState* button = new ButtonState();
        button->pin = pin;
        button->mode = mode;
        button->pressTimeout = pressTimeout;
        button->handler = handler;

        // Install GPIO ISR
        gpio_install_isr_service(0);
        gpio_isr_handler_add(pin, handleButtonInterrupt, button);
        gpio_set_intr_type(pin, GPIO_INTR_ANYEDGE);
    }
};

}    // namespace farmhub::kernel
