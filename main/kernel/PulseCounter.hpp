#pragma once

#include <atomic>

#include <driver/gpio.h>

#include <kernel/Pin.hpp>

#include <Arduino.h>

namespace farmhub::kernel {

/**
 * Interrupt-based pulse-counter for low-frequency signals.
 */
class PulseCounter {
private:
    static void IRAM_ATTR interruptHandler(void* arg);

public:
    PulseCounter(InternalPinPtr pin, microseconds maxGlitchDuration)
        : pin(pin)
        , maxGlitchDuration(maxGlitchDuration) {
        // Remove once we drop Arduino
        pin->pinMode(INPUT);
        // Configure the GPIO pin as an input
        gpio_config_t config = {
            .pin_bit_mask = 1ULL << pin->getGpio(),
            .mode = GPIO_MODE_INPUT,
            .pull_up_en = GPIO_PULLUP_DISABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type = GPIO_INTR_ANYEDGE,
        };
        gpio_config(&config);

        // Install GPIO ISR service
        // gpio_install_isr_service(0);

        // Attach the ISR handler to the GPIO pin
        gpio_isr_handler_add(pin->getGpio(), interruptHandler, this);

        LOGTD(Tag::PCNT, "Registered interrupt-based pulse counter unit on pin %s",
            pin->getName().c_str());
    }

    uint32_t getCount() const {
        uint32_t count = counter.load();
        LOGTV(Tag::PCNT, "Counted %lu pulses on pin %s",
            count, pin->getName().c_str());
        return count;
    }

    uint32_t reset() {
        uint32_t count = counter.exchange(0);
        LOGTV(Tag::PCNT, "Counted %lu pulses and cleared on pin %s",
            count, pin->getName().c_str());
        return count;
    }

    PinPtr getPin() const {
        return pin;
    }

private:
    const InternalPinPtr pin;
    const microseconds maxGlitchDuration;
    std::optional<time_point<boot_clock>> lastRaisingEdgeTime;
    std::atomic<uint32_t> counter { 0 };
};

void IRAM_ATTR PulseCounter::interruptHandler(void* arg) {
    auto self = static_cast<PulseCounter*>(arg);
    if (self->maxGlitchDuration > microseconds::zero()) {
        auto state = self->pin->digitalRead();
        if (state == 1) {
            self->lastRaisingEdgeTime = boot_clock::now();
        } else {
            if (self->lastRaisingEdgeTime.has_value()) {
                if (boot_clock::now() - self->lastRaisingEdgeTime.value() > self->maxGlitchDuration) {
                    self->counter++;
                } else {
                    printf("Last raising edge is too young at %lld\n", (boot_clock::now() - self->lastRaisingEdgeTime.value()).count());
                }
                self->lastRaisingEdgeTime.reset();
            }
        }
    } else {
        self->counter++;
    }
}

}    // namespace farmhub::kernel
