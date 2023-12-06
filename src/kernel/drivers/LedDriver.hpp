#pragma once

#include <atomic>
#include <chrono>
#include <list>

#include <kernel/Task.hpp>

using namespace std::chrono;

namespace farmhub { namespace kernel { namespace drivers {

class LedDriver {
public:
    LedDriver(const char* name, gpio_num_t pin, std::list<milliseconds> initialPattern = { -milliseconds::max() })
        : pin(pin)
        , pattern(initialPattern) {
        pinMode(pin, OUTPUT);
        Task::loop(name, [this](Task& task) {
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
            std::list<milliseconds>* newPattern;
            if (xQueueReceive(patternQueue, &newPattern, pdMS_TO_TICKS(abs(delay.count()))) == pdTRUE) {
                pattern = *newPattern;
                currentPattern = {};
                free(newPattern);
            }
        });
    }

    void setPattern(std::list<milliseconds> pattern) {
        auto* payload = new std::list<milliseconds>(pattern);
        xQueueSend(patternQueue, &payload, portMAX_DELAY);
    }

    void turnOn() {
        setPattern({ milliseconds::max() });
    }

    void turnOff() {
        setPattern({ -milliseconds::max() });
    }

    void blink(milliseconds blinkRate) {
        setPattern(ledState
                ? std::list<milliseconds>({ blinkRate, -blinkRate })
                : std::list<milliseconds>({ -blinkRate, blinkRate }));
    }

private:
    void setLedState(bool state) {
        this->ledState = state;
        digitalWrite(pin, state);
    }

    QueueHandle_t patternQueue { xQueueCreate(1, sizeof(std::list<milliseconds>*)) };
    const gpio_num_t pin;
    std::atomic<bool> ledState;
    std::list<milliseconds> pattern;
    std::list<milliseconds> currentPattern;
};

}}}    // namespace farmhub::kernel::drivers
