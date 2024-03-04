#pragma once

#include <map>

#include <ArduinoJson.h>

#include <kernel/Task.hpp>

namespace farmhub::kernel {

class TelemetryProvider {
public:
    virtual void populateTelemetry(JsonObject& json) = 0;
};

class TelemetryCollector {
public:
    void collect(JsonObject& root) {
        root["uptime"] = millis();
        for (auto& entry : providers) {
            auto& name = entry.first;
            auto& provider = entry.second;
            JsonObject telemetryRoot = root.createNestedObject(name);
            provider.get().populateTelemetry(telemetryRoot);
        }
    }

    void registerProvider(const String& name, TelemetryProvider& provider) {
        // TODO Check for duplicates
        providers.emplace(name, std::reference_wrapper<TelemetryProvider>(provider));
    }

private:
    std::map<String, std::reference_wrapper<TelemetryProvider>> providers;
};

class TelemetryPublisher {
public:
    virtual void publishTelemetry() = 0;
};

}    // namespace farmhub::kernel
