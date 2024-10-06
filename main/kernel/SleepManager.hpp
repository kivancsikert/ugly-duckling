#pragma once

#include <esp_pm.h>

#include <Arduino.h>

#include <kernel/Concurrent.hpp>
#include <kernel/Log.hpp>

// FIXME Why do we need to define these manually?
#if CONFIG_IDF_TARGET_ESP32
#error "ESP32 is not supported"
#elif CONFIG_IDF_TARGET_ESP32S2
typedef esp_pm_config_esp32s2_t esp_pm_config_t;
#define MAX_CPU_FREQ_MHZ CONFIG_ESP32S2_DEFAULT_CPU_FREQ_MHZ
#define MIN_CPU_FREQ_MHZ 80
#elif CONFIG_IDF_TARGET_ESP32S3
typedef esp_pm_config_esp32s3_t esp_pm_config_t;
#define MAX_CPU_FREQ_MHZ CONFIG_ESP32S3_DEFAULT_CPU_FREQ_MHZ
#define MIN_CPU_FREQ_MHZ 40
#endif

namespace farmhub::kernel {

class SleepManager {
public:
    SleepManager(bool requestedSleepWhenIdle)
        : sleepWhenIdle(shouldSleepWhenIdle(requestedSleepWhenIdle)) {
        if (sleepWhenIdle) {
            configurePowerManagement(true);
        } else {
            // We keep awake forever
            keepAwake();
        }
    }

    static bool shouldSleepWhenIdle(bool requestedSleepWhenIdle) {
        if (requestedSleepWhenIdle) {
#if FARMHUB_DEBUG
            Log.warn("Light sleep is disabled in debug mode");
            return false;
#elif not(CONFIG_PM_ENABLE)
            Log.info("Power management is disabled because CONFIG_PM_ENABLE is not set");
            return false;
#elif not(CONFIG_FREERTOS_USE_TICKLESS_IDLE)
            Log.info("Light sleep is disabled because CONFIG_FREERTOS_USE_TICKLESS_IDLE is not set");
            return false;
#else
            Log.info("Light sleep is enabled");
            return true;
#endif
        } else {
            Log.info("Light sleep is disabled");
            return false;
        }
    }

    const bool sleepWhenIdle;

    void keepAwake() {
        Lock lock(requestCountMutex);
        requestCount++;
        Log.debug("Task %s requested the device to keep awake, counter at %d",
            pcTaskGetName(nullptr), requestCount);
        if (requestCount == 1) {
            configurePowerManagement(false);
        }
    }

    void allowSleep() {
        Lock lock(requestCountMutex);
        requestCount--;
        Log.debug("Task %s finished with insomniac activity, counter at %d",
            pcTaskGetName(nullptr), requestCount);
        if (requestCount == 0) {
            configurePowerManagement(true);
        }
    }

private:
    void configurePowerManagement(bool enableLightSleep) {
        Log.trace("Configuring power management, CPU max/min at %d/%d MHz, light sleep is %s",
            MAX_CPU_FREQ_MHZ, MIN_CPU_FREQ_MHZ, enableLightSleep ? "enabled" : "disabled");
        // Configure dynamic frequency scaling:
        // maximum and minimum frequencies are set in sdkconfig,
        // automatic light sleep is enabled if tickless idle support is enabled.
        esp_pm_config_t pm_config = {
            .max_freq_mhz = MAX_CPU_FREQ_MHZ,
            .min_freq_mhz = MIN_CPU_FREQ_MHZ,
            .light_sleep_enable = enableLightSleep
        };
        ESP_ERROR_CHECK(esp_pm_configure(&pm_config));
    }

    Mutex requestCountMutex;
    int requestCount = 0;
};

class KeepAwake {
public:
    KeepAwake(SleepManager& manager)
        : manager(manager) {
        manager.keepAwake();
    }

    ~KeepAwake() {
        manager.allowSleep();
    }

    // Delete copy constructor and assignment operator to prevent copying
    KeepAwake(const KeepAwake&) = delete;
    KeepAwake& operator=(const KeepAwake&) = delete;

private:
    SleepManager& manager;
};

}    // namespace farmhub::kernel
