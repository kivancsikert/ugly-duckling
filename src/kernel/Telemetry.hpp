#pragma once

#include <map>

#include <ArduinoJson.h>

#include <kernel/Task.hpp>

namespace farmhub { namespace kernel {

class TelemetryProvider {
    virtual void populateTelemetry(JsonObject& json) = 0;
    friend class TelemetryCollector;
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

}}    // namespace farmhub::kernel
