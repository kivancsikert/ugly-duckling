#pragma once

#include <atomic>
#include <memory>

#include <driver/gpio.h>
#include <driver/rtc_io.h>

#include <kernel/BootClock.hpp>
#include <kernel/Concurrent.hpp>
#include <kernel/Log.hpp>
#include <kernel/Pin.hpp>
#include <kernel/PowerManager.hpp>

namespace farmhub::kernel {

/**
 * Interrupt-based pulse-counter for low-frequency signals.
 *
 * It uses the RTC GPIO matrix to detect rising and falling edges on a GPIO pin.
 * Keeps the device out of light sleep while a pulse is being detected.
 *
 */
class PulseCounter {
public:
    PulseCounter(InternalPinPtr pin)
        : pin(pin) {
        auto gpio = pin->getGpio();

        // Configure the GPIO pin as an input
        gpio_config_t config = {
            .pin_bit_mask = 1ULL << gpio,
            .mode = GPIO_MODE_INPUT,
            .pull_up_en = GPIO_PULLUP_DISABLE,
            .pull_down_en = GPIO_PULLDOWN_ENABLE,
            .intr_type = GPIO_INTR_ANYEDGE,
        };
        ESP_ERROR_CHECK(gpio_config(&config));

        // Use the same configuration while in light sleep
        ESP_ERROR_CHECK(gpio_sleep_sel_dis(gpio));

        // TODO Where should this be called?
        ESP_ERROR_CHECK(esp_sleep_enable_gpio_wakeup());

        // Attach the ISR handler to the GPIO pin
        ESP_ERROR_CHECK(gpio_isr_handler_add(gpio, interruptHandler, this));

        LOGTD(Tag::PCNT, "Registered interrupt-based pulse counter unit on pin %s",
            pin->getName().c_str());

        // TODO Turn this into a single task that handles all pulse counters
        Task::run(pin->getName(), 4096, [this](Task& task) {
            runLoop();
        });
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
    static void IRAM_ATTR interruptHandler(void* arg);

    // The amount of time we wait for the end of a pulse before we consider it a timeout
    static constexpr microseconds maxHighTime = 100ms;

    enum class EdgeKind {
        Rising,
        Falling,
    };

    void runLoop() {
        std::optional<time_point<boot_clock>> lastRisingEdge;
        EdgeKind wakeToEdge = EdgeKind::Falling;
        EdgeKind wakeToEdgeNext = EdgeKind::Rising;
        std::optional<PowerManagementLockGuard> preventLightSleep;

        while (true) {
            ticks timeout;
            if (lastRisingEdge.has_value()) {
                auto elapsedSinceLastRisingEdge = boot_clock::now() - lastRisingEdge.value();
                if (elapsedSinceLastRisingEdge > maxHighTime) {
                    LOGTV(Tag::PCNT, "Timeout while waiting for falling edge on pin %s",
                        pin->getName().c_str());
                    // We've timed out, let the device sleep again, and forget this rising edge
                    // as it was clearly not the beginning of an actual pulse
                    lastRisingEdge.reset();
                    preventLightSleep.reset();
                    wakeToEdgeNext = EdgeKind::Falling;
                    timeout = ticks::max();
                } else {
                    timeout = floor<ticks>(maxHighTime - elapsedSinceLastRisingEdge);
                }
            } else {
                timeout = ticks::max();
            }

            // Make sure that we wake up on the correct edge next
            if (wakeToEdge != wakeToEdgeNext) {
                auto intrType = wakeToEdgeNext == EdgeKind::Rising
                    ? GPIO_INTR_HIGH_LEVEL
                    : GPIO_INTR_LOW_LEVEL;
                ESP_ERROR_CHECK(rtc_gpio_wakeup_enable(pin->getGpio(), intrType));
                wakeToEdge = wakeToEdgeNext;
            }

            for (auto event = eventQueue.pollIn(timeout); event.has_value(); event = eventQueue.poll()) {
                switch (event.value()) {
                    case EdgeKind::Rising:
                        if (!lastRisingEdge.has_value()) {
                            // Beginning of new pulse detected, prevent light sleep
                            // until the end of the pulse or a timeout
                            lastRisingEdge = boot_clock::now();
                            preventLightSleep.emplace(PowerManager::lightSleepLock);
                        }
                        break;
                    case EdgeKind::Falling:
                        if (lastRisingEdge.has_value()) {
                            // End of pulse detected, increase count and let the device sleep again
                            counter++;
                            lastRisingEdge.reset();
                            preventLightSleep.reset();
                        }
                        wakeToEdgeNext = EdgeKind::Rising;
                        break;
                }
            }
        }
    }

    const InternalPinPtr pin;
    std::atomic<uint32_t> counter { 0 };

    CopyQueue<EdgeKind> eventQueue { pin->getName(), 16 };
};

void IRAM_ATTR PulseCounter::interruptHandler(void* arg) {
    auto self = static_cast<PulseCounter*>(arg);
    self->eventQueue.offerFromISR(self->pin->digitalRead()
            ? EdgeKind::Rising
            : EdgeKind::Falling);
}

}    // namespace farmhub::kernel
