#pragma once

#include <list>
#include <memory>

#include <esp_sleep.h>

#include <kernel/MovingAverage.hpp>
#include <kernel/ShutdownManager.hpp>
#include <kernel/Task.hpp>
#include <kernel/Telemetry.hpp>
#include <kernel/drivers/BatteryDriver.hpp>

using namespace farmhub::kernel::drivers;

namespace farmhub::kernel {

/**
 * @brief Time to wait between battery checks.
 */
static constexpr microseconds LOW_POWER_SLEEP_CHECK_INTERVAL = 10s;

[[noreturn]] inline void enterLowPowerDeepSleep() {
    printf("Entering low power deep sleep\n");
    esp_deep_sleep(duration_cast<microseconds>(LOW_POWER_SLEEP_CHECK_INTERVAL).count());
    // Signal to the compiler that we are not returning for real
    abort();
}

class BatteryManager : public TelemetryProvider {
public:
    BatteryManager(
        std::shared_ptr<BatteryDriver> battery,
        double batteryShutdownThreshold,
        std::shared_ptr<ShutdownManager> shutdownManager)
        : battery(battery)
        , batteryShutdownThreshold(batteryShutdownThreshold)
        , shutdownManager(shutdownManager) {
        Task::loop("battery", 2560, [this](Task& task) {
            checkBatteryVoltage(task);
        });
    }

    void populateTelemetry(JsonObject& json) override {
        auto voltage = batteryVoltage.getAverage();
        if (voltage > 0) {
            json["voltage"] = voltage;
        }
    }

private:
    void checkBatteryVoltage(Task& task) {
        task.delayUntil(LOW_POWER_CHECK_INTERVAL);
        auto currentVoltage = battery->getVoltage();
        batteryVoltage.record(currentVoltage);
        auto voltage = batteryVoltage.getAverage();

        if (voltage != 0.0 && voltage < batteryShutdownThreshold) {
            LOGI("Battery voltage low (%.2f V < %.2f), starting shutdown process, will go to deep sleep in %lld seconds",
                voltage, batteryShutdownThreshold, duration_cast<seconds>(LOW_BATTERY_SHUTDOWN_TIMEOUT).count());

            // TODO Publish all MQTT messages, then shut down WiFi, and _then_ start shutting down peripherals
            //      Doing so would result in less of a power spike, which can be important if the battery is already low

            shutdownManager->startShutdown();
            Task::delay(LOW_BATTERY_SHUTDOWN_TIMEOUT);
            enterLowPowerDeepSleep();
        }
    };

    const std::shared_ptr<BatteryDriver> battery;
    const double batteryShutdownThreshold;
    const std::shared_ptr<ShutdownManager> shutdownManager;

    MovingAverage<double> batteryVoltage { 5 };

    /**
     * @brief How often we check the battery voltage while in operation.
     *
     * We use a prime number to avoid synchronizing with other tasks.
     */
    static constexpr auto LOW_POWER_CHECK_INTERVAL = 10313ms;

    /**
     * @brief Time to wait for shutdown process to finish before going to deep sleep.
     */
    static constexpr auto LOW_BATTERY_SHUTDOWN_TIMEOUT = 10s;
};

}    // namespace farmhub::kernel
