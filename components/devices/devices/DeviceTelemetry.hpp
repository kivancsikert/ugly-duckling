#pragma once

#include <chrono>
#include <memory>

using namespace farmhub::kernel;

namespace farmhub::devices {

class MemoryTelemetryProvider final : public TelemetryProvider {
public:
    void populateTelemetry(JsonObject& json) override {
        json["free-heap"] = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
        json["min-heap"] = heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL);
    }
};

}    // namespace farmhub::devices
