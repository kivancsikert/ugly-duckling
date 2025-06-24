#pragma once

#include <list>
#include <memory>

#include <ArduinoJson.h>

#include <BootClock.hpp>
#include <Task.hpp>
#include <utility>

namespace farmhub::kernel {

class TelemetryProvider {
public:
    virtual ~TelemetryProvider() = default;

    virtual void populateTelemetry(JsonObject& json) = 0;
};

class TelemetryCollector {
public:
    void collect(JsonObject& root) {
        root["uptime"] = duration_cast<milliseconds>(boot_clock::now().time_since_epoch()).count();
        root["timestamp"] = duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
        auto entries = root["entries"].to<JsonArray>();
        for (auto& provider : providers) {
            auto entry = entries.add<JsonObject>();
            entry["type"] = provider.type;
            if (!provider.name.empty()) {
                entry["name"] = provider.name;
            }
            auto data = entry["data"].to<JsonObject>();
            provider.populate(data);
        }
    }

    void registerProvider(
        const std::string& type,
        const std::string& name,
        std::function<void(JsonObject&)> populate) {
        LOGV("Registering telemetry provider %s of type %s", name.c_str(), type.c_str());
        providers.push_back({ name, type, std::move(populate) });
    }

private:
    struct Provider {
        std::string name;
        std::string type;
        std::function<void(JsonObject&)> populate;
    };

    std::list<Provider> providers;
};

}    // namespace farmhub::kernel
