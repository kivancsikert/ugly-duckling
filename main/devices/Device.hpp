#pragma once

#include <chrono>
#include <memory>

#include <esp_netif.h>
#include <esp_pm.h>
#include <esp_wifi.h>

#include <kernel/BootClock.hpp>
#include <kernel/Concurrent.hpp>
#include <kernel/CrashManager.hpp>
#include <kernel/DebugConsole.hpp>
#include <kernel/Strings.hpp>
#include <kernel/Task.hpp>
#include <kernel/mqtt/MqttDriver.hpp>
#include <kernel/mqtt/MqttRoot.hpp>
#include <kernel/mqtt/MqttTelemetryPublisher.hpp>

using namespace std::chrono;
using namespace std::chrono_literals;
using namespace farmhub::kernel;
using namespace farmhub::kernel::drivers;
using namespace farmhub::kernel::mqtt;

#if defined(MK4)
#include <devices/UglyDucklingMk4.hpp>
typedef farmhub::devices::UglyDucklingMk4 TDeviceDefinition;
typedef farmhub::devices::Mk4Config TDeviceConfiguration;
#elif defined(MK5)
#include <devices/UglyDucklingMk5.hpp>
typedef farmhub::devices::UglyDucklingMk5 TDeviceDefinition;
typedef farmhub::devices::Mk5Config TDeviceConfiguration;
#elif defined(MK6)
#include <devices/UglyDucklingMk6.hpp>
typedef farmhub::devices::UglyDucklingMk6 TDeviceDefinition;
typedef farmhub::devices::Mk6Config TDeviceConfiguration;
#elif defined(MK7)
#include <devices/UglyDucklingMk7.hpp>
typedef farmhub::devices::UglyDucklingMk7 TDeviceDefinition;
typedef farmhub::devices::Mk7Config TDeviceConfiguration;
#elif defined(MK8)
#include <devices/UglyDucklingMk8.hpp>
typedef farmhub::devices::UglyDucklingMk8 TDeviceDefinition;
typedef farmhub::devices::Mk8Config TDeviceConfiguration;
#else
#error "No device defined"
#endif

using namespace farmhub::kernel;

namespace farmhub::devices {

class MemoryTelemetryProvider : public TelemetryProvider {
public:
    void populateTelemetry(JsonObject& json) override {
        json["free-heap"] = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
        json["min-heap"] = heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL);
    }
};

class WiFiTelemetryProvider : public TelemetryProvider {
public:
    WiFiTelemetryProvider(const std::shared_ptr<WiFiDriver> wifi)
        : wifi(wifi) {
    }

    void populateTelemetry(JsonObject& json) override {
        json["uptime"] = wifi->getUptime().count();
    }

private:
    const std::shared_ptr<WiFiDriver> wifi;
};

class PowerManagementTelemetryProvider : public TelemetryProvider {
public:
    PowerManagementTelemetryProvider(std::shared_ptr<PowerManager> powerManager)
        : powerManager(powerManager) {
    }

    void populateTelemetry(JsonObject& json) override {
#ifdef CONFIG_PM_LIGHT_SLEEP_CALLBACKS
        json["sleep-time"] = powerManager->getLightSleepTime().count();
        json["sleep-count"] = powerManager->getLightSleepCount();
#endif
    }

private:
    const std::shared_ptr<PowerManager> powerManager;
};

}    // namespace farmhub::devices
