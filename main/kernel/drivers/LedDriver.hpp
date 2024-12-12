#pragma once

#include <atomic>
#include <chrono>
#include <vector>

#include <kernel/Concurrent.hpp>
#include <kernel/Pin.hpp>
#include <kernel/Task.hpp>

using namespace std::chrono;

using farmhub::kernel::PinPtr;

namespace farmhub::kernel::drivers {

class LedDriver {
public:
    typedef std::vector<milliseconds> BlinkPattern;

    LedDriver(const String& name, PinPtr pin)
        : pin(pin)
        , patternQueue(name, 1)
        , pattern({ -milliseconds::max() }) {
        LOGI("Initializing LED driver on pin %s",
            pin->getName().c_str());

        pin->pinMode(Pin::Mode::Output);
        Task::loop(name, 2048, [this](Task& task) {
            handleIteration();
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

    void blinkPatternInMs(std::vector<int> pattern) {
        blinkPattern(BlinkPattern(pattern.begin(), pattern.end()));
    }

    void blinkPattern(const BlinkPattern& pattern) {
        if (pattern.empty()) {
            turnOff();
        } else {
            setPattern(pattern);
        }
    }

private:
    void handleIteration() {
        if (cursor == pattern.end()) {
            cursor = pattern.begin();
        }
        milliseconds blinkTime = *cursor;
        cursor++;

        if (blinkTime > milliseconds::zero()) {
            setLedState(0);
        } else {
            setLedState(1);
        }

        // TOOD Substract processing time from delay
        ticks timeout = blinkTime < milliseconds::zero() ? -blinkTime : blinkTime;
        patternQueue.pollIn(timeout, [this](const BlinkPattern& newPattern) {
            pattern = newPattern;
            cursor = pattern.cbegin();
        });
    }

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
    std::vector<milliseconds>::const_iterator cursor = pattern.cbegin();
    std::atomic<bool> ledState;
};

}    // namespace farmhub::kernel::drivers
