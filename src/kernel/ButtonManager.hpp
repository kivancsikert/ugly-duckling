#pragma once

#include <chrono>
#include <functional>
#include <map>

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

static ButtonState button0;
static void IRAM_ATTR triggerButton0Isr() {
    buttonStateInterrupts.offerFromISR(&button0);
}
static ButtonState button1;
static void IRAM_ATTR triggerButton1Isr() {
    buttonStateInterrupts.offerFromISR(&button1);
}
static ButtonState button2;
static void IRAM_ATTR triggerButton2Isr() {
    buttonStateInterrupts.offerFromISR(&button2);
}
static ButtonState button3;
static void IRAM_ATTR triggerButton3Isr() {
    buttonStateInterrupts.offerFromISR(&button3);
}

class ButtonManager {
public:
    ButtonManager() {
        Task::loop("button-manager", [this](Task& task) {
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
        pinMode(pin, mode == ButtonMode::PullUp ? INPUT_PULLUP : INPUT_PULLDOWN);

        void (*interruptHandler)();
        ButtonState* button;
        switch (registeredButtonCount++) {
            case 0:
                button = &button0;
                interruptHandler = triggerButton0Isr;
                break;
            case 1:
                button = &button0;
                interruptHandler = triggerButton1Isr;
                break;
            case 2:
                button = &button1;
                interruptHandler = triggerButton2Isr;
                break;
            case 3:
                button = &button2;
                interruptHandler = triggerButton3Isr;
                break;
            default:
                throw new std::runtime_error("Too many buttons");
        }
        button->pin = pin;
        button->mode = mode;
        button->pressTimeout = pressTimeout;
        button->handler = handler;

        attachInterrupt(digitalPinToInterrupt(pin), interruptHandler, CHANGE);
    }

private:
    int registeredButtonCount = 0;
};

}    // namespace farmhub::kernel
