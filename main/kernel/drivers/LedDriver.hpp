#pragma once

#include <atomic>
#include <chrono>
#include <list>

#include <kernel/Concurrent.hpp>
#include <kernel/Log.hpp>
#include <kernel/Pin.hpp>
#include <kernel/Task.hpp>

using namespace std::chrono;

using farmhub::kernel::PinPtr;

namespace farmhub::kernel::drivers {

class LedDriver {
public:
    typedef std::list<milliseconds> BlinkPattern;

    LedDriver(const String& name, PinPtr pin)
        : pin(pin)
        , patternQueue(name, 1)
        , pattern({ -milliseconds::max() }) {
        Log.info("Initializing LED driver on pin %s",
            pin->getName().c_str());

        pin->pinMode(OUTPUT);
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
    void setPattern(const BlinkPattern& pattern) {
        patternQueue.put(pattern);
    }

    void setLedState(bool state) {
        this->ledState = state;
        pin->digitalWrite(state);
    }

    const PinPtr pin;
    Queue<BlinkPattern> patternQueue;
    BlinkPattern pattern;
    std::atomic<bool> ledState;
    BlinkPattern currentPattern;
};

}    // namespace farmhub::kernel::drivers
