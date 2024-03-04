#pragma once

#include <atomic>
#include <chrono>
#include <list>

#include <ArduinoLog.h>

#include <kernel/Concurrent.hpp>
#include <kernel/Task.hpp>

using namespace std::chrono;

namespace farmhub::kernel::drivers {

class LedDriver {
public:
    typedef std::list<milliseconds> BlinkPattern;

    LedDriver(const String& name, gpio_num_t pin)
        : pin(pin)
        , patternQueue(name, 1)
        , pattern({ -milliseconds::max() }) {
        Log.infoln("Initializing LED driver on pin %d",
            pin);

        pinMode(pin, OUTPUT);
        Task::loop(name, 2048, [this](Task& task) {
            if (currentPattern.empty()) {
                currentPattern = pattern;
            }
            milliseconds blinkTime = currentPattern.front();
            currentPattern.pop_front();

            if (blinkTime > milliseconds::zero()) {
                setLedState(LOW);
            } else {
                setLedState(HIGH);
            }

            // TOOD Substract processing time from delay
            ticks timeout = blinkTime < milliseconds::zero() ? -blinkTime : blinkTime;
            patternQueue.pollIn(timeout, [this](const BlinkPattern& newPattern) {
                pattern = newPattern;
                currentPattern = {};
            });
        });
    }

    void turnOn() {
        setPattern({ milliseconds::max() });
    }

    void turnOff() {
        setPattern({ -milliseconds::max() });
    }

    void blink(milliseconds blinkRate) {
        setPattern({ blinkRate / 2, -blinkRate / 2 });
    }

    void blinkPatternInMs(std::list<int> pattern) {
        blinkPattern(BlinkPattern(pattern.begin(), pattern.end()));
    }

    void blinkPattern(BlinkPattern pattern) {
        if (pattern.empty()) {
            turnOff();
        } else {
            setPattern(pattern);
        }
    }

private:
    void setPattern(BlinkPattern pattern) {
        patternQueue.put(pattern);
    }

    void setLedState(bool state) {
        this->ledState = state;
        digitalWrite(pin, state);
    }

    const gpio_num_t pin;
    Queue<BlinkPattern> patternQueue;
    BlinkPattern pattern;
    std::atomic<bool> ledState;
    BlinkPattern currentPattern;
};

}    // namespace farmhub::kernel::drivers
