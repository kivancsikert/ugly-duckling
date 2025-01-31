#pragma once

#include <chrono>
#include <memory>

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
