#pragma once

#include <atomic>
#include <chrono>
#include <list>

#include <kernel/Task.hpp>

using namespace std::chrono;

namespace farmhub { namespace kernel { namespace drivers {

class LedDriver {
public:
    typedef std::list<milliseconds> BlinkPattern;

    LedDriver(const char* name, gpio_num_t pin)
        : pin(pin)
        , pattern({ -milliseconds::max() }) {
        pinMode(pin, OUTPUT);
        Task::loop(name, 2048, [this](Task& task) {
            if (currentPattern.empty()) {
                currentPattern = pattern;
            }
            milliseconds delay = currentPattern.front();
            currentPattern.pop_front();

            if (delay > milliseconds::zero()) {
                setLedState(LOW);
            } else {
                setLedState(HIGH);
            }
            BlinkPattern* newPattern;
            if (xQueueReceive(patternQueue, &newPattern, pdMS_TO_TICKS(abs(delay.count()))) == pdTRUE) {
                pattern = *newPattern;
                currentPattern = {};
                free(newPattern);
            }
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
        auto* payload = new BlinkPattern(pattern);
        xQueueSend(patternQueue, &payload, portMAX_DELAY);
    }

    void setLedState(bool state) {
        this->ledState = state;
        digitalWrite(pin, state);
    }

    QueueHandle_t patternQueue { xQueueCreate(1, sizeof(BlinkPattern*)) };
    const gpio_num_t pin;
    std::atomic<bool> ledState;
    BlinkPattern pattern;
    BlinkPattern currentPattern;
};

}}}    // namespace farmhub::kernel::drivers
