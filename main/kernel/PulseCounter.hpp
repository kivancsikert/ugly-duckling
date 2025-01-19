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

        // Make sure we handle the state change when the device wakes up due to a GPIO interrupt
        esp_pm_sleep_cbs_register_config_t sleepCallbackConfig = {
            .exit_cb = [](int64_t timeSleptInUs, void* arg) {
                if (esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_GPIO) {
                    auto self = static_cast<PulseCounter*>(arg);
                    self->handlePotentialStateChange();
                }
                return ESP_OK; },
            .exit_cb_user_arg = this,
        };
        ESP_ERROR_CHECK(esp_pm_light_sleep_register_cbs(&sleepCallbackConfig));

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
    enum class EdgeKind {
        Rising,
        Falling,
    };

    static void IRAM_ATTR interruptHandler(void* arg) {
        auto self = static_cast<PulseCounter*>(arg);
        self->handlePotentialStateChange();
    }

    IRAM_ATTR void handlePotentialStateChange() {
        eventQueue.offerFromISR(takeSample());
    }

    IRAM_ATTR EdgeKind takeSample() {
        return pin->digitalRead()
            ? EdgeKind::Rising
            : EdgeKind::Falling;
    }

    // The amount of time to keep the device awake after detecting an edge to make sure we detect the next edge, too
    static constexpr microseconds maxKeepAwakeTime = 10ms;

    void runLoop() {
        std::optional<std::pair<PowerManagementLockGuard, time_point<boot_clock>>> sleepLock;
        EdgeKind lastEdge = takeSample();
        bool seenNewEdge = true;

        while (true) {
            if (seenNewEdge) {
                // Got a new edge, renew keep-alive to make sure we detect the next edge
                sleepLock.emplace(PowerManager::noLightSleep, boot_clock::now());

                // Make sure we wake up again to check for the opposing edge
                ESP_ERROR_CHECK(rtc_gpio_wakeup_enable(
                    pin->getGpio(),
                    lastEdge == EdgeKind::Rising
                        ? GPIO_INTR_LOW_LEVEL
                        : GPIO_INTR_HIGH_LEVEL));
            }

            ticks timeout;
            if (sleepLock.has_value()) {
                auto elapsedSinceSleepLock = boot_clock::now() - sleepLock.value().second;
                if (elapsedSinceSleepLock > maxKeepAwakeTime) {
                    LOGTV(Tag::PCNT, "Timeout while waiting for falling edge on pin %s",
                        pin->getName().c_str());
                    // We've timed out, let the device sleep again until the next edge
                    sleepLock.reset();
                    timeout = ticks::max();
                } else {
                    // Wait at most until the sleep lock would time out
                    timeout = floor<ticks>(maxKeepAwakeTime - elapsedSinceSleepLock);
                }
            } else {
                // No sleep lock, wait indefinitely for the next edge
                timeout = ticks::max();
            }

            seenNewEdge = false;
            for (auto event = eventQueue.pollIn(timeout); event.has_value(); event = eventQueue.poll()) {
                auto edge = event.value();
                if (edge != lastEdge) {
                    lastEdge = edge;
                    seenNewEdge = true;
                    if (lastEdge == EdgeKind::Falling) {
                        counter++;
                    }
                }
            }
        }
    }

    const InternalPinPtr pin;
    std::atomic<uint32_t> counter { 0 };

    CopyQueue<EdgeKind> eventQueue { pin->getName(), 16 };
};

}    // namespace farmhub::kernel
