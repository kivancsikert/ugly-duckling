#pragma once

#include <atomic>
#include <chrono>

#include <kernel/Task.hpp>

using namespace std::chrono;

namespace farmhub { namespace kernel { namespace drivers {

class LedDriver : LoopTask {
public:
    enum class State {
        OFF,
        ON,
        BLINKING
    };

    LedDriver(const char* name, gpio_num_t pin, State initialState = State::OFF, milliseconds blinkRate = seconds(1))
        : LoopTask(name)
        , pin(pin)
        , state(initialState)
        , blinkRate(blinkRate) {
        pinMode(pin, OUTPUT);
    }

    void loop() override {
        State state = this->state;
        switch (state) {
            case State::OFF:
                setLedState(HIGH);
                suspend();
                break;
            case State::ON:
                setLedState(LOW);
                suspend();
                break;
            case State::BLINKING:
                setLedState(!ledState);
                delayUntil(blinkRate.load() / 2);
                break;
        }
    }

    void setState(State state) {
        if (this->state != state) {
            this->state = state;
            abortDelay();
        }
    }

    void setBlinkRate(milliseconds blinkRate) {
        this->blinkRate = blinkRate;
    }

private:
    void setLedState(bool state) {
        this->ledState = state;
        digitalWrite(pin, state);
    }

    const gpio_num_t pin;
    std::atomic<State> state;
    std::atomic<milliseconds> blinkRate;
    bool ledState;
};

}}}    // namespace farmhub::kernel::drivers
