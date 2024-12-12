#pragma once

#include <map>
#include <memory>

#include <ArduinoJson.h>

#include <kernel/BootClock.hpp>
#include <kernel/Task.hpp>

namespace farmhub::kernel {

class TelemetryProvider {
public:
    virtual void populateTelemetry(JsonObject& json) = 0;
};

class TelemetryCollector {
public:
    void collect(JsonObject& root) {
        root["uptime"] = duration_cast<milliseconds>(boot_clock::now().time_since_epoch()).count();
        for (auto& entry : providers) {
            auto& name = entry.first;
            auto& provider = entry.second;
            JsonObject telemetryRoot = root[name].to<JsonObject>();
            provider->populateTelemetry(telemetryRoot);
        }
    }

    void registerProvider(const String& name, std::shared_ptr<TelemetryProvider> provider) {
        LOGV("Registering telemetry provider %s", name.c_str());
        // TODO Check for duplicates
        providers.emplace(name, provider);
    }

private:
    std::map<String, std::shared_ptr<TelemetryProvider>> providers;
};

class TelemetryPublisher {
public:
    virtual void publishTelemetry() = 0;
};

}    // namespace farmhub::kernel
