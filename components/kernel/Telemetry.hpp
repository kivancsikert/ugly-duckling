#pragma once

#include <list>
#include <memory>
#include <utility>

#include <ArduinoJson.h>

#include <Concurrent.hpp>

namespace farmhub::kernel {

class TelemetryCollector {
public:
    void collect(JsonArray& entries) {
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

class TelemetryPublisher {
public:
    explicit TelemetryPublisher(std::shared_ptr<CopyQueue<bool>> telemetryPublishQueue)
        : telemetryPublishQueue(std::move(telemetryPublishQueue)) {}

    void requestTelemetryPublishing() {
        telemetryPublishQueue->overwrite(true);
    }

private:
    std::shared_ptr<CopyQueue<bool>> telemetryPublishQueue;
};

}    // namespace farmhub::kernel
