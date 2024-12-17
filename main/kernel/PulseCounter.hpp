#pragma once

#include <atomic>

#include <driver/gpio.h>

#include <kernel/Pin.hpp>

namespace farmhub::kernel {

/**
 * Interrupt-based pulse-counter for low-frequency signals.
 */
class PulseCounter {
public:
    PulseCounter(InternalPinPtr pin)
        : pin(pin) {
        // Configure the GPIO pin as an input
        gpio_config_t config = {
            .pin_bit_mask = 1ULL << pin->getGpio(),
            .mode = GPIO_MODE_INPUT,
            .pull_up_en = GPIO_PULLUP_DISABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE   ,
            .intr_type = GPIO_INTR_POSEDGE,
        };
        gpio_config(&config);

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
    static void IRAM_ATTR interruptHandler(void *arg) {
        auto self = static_cast<PulseCounter*>(arg);
        self->counter++;
    }

    const InternalPinPtr pin;
    std::atomic<uint32_t> counter { 0 };
};

}    // namespace farmhub::kernel
