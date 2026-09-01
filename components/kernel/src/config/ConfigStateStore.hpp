#pragma once

#include "ConfigState.hpp"
#include <Log.hpp>
#include <NvsStore.hpp>

#include <ArduinoJson.h>

#include <memory>
#include <utility>

namespace cornucopia::ugly_duckling::kernel::config {

/**
 * @brief Owns reading/writing the device's ConfigState in NVS (docs/Configuration.md,
 * "Storage: envelopes and slots"). load() returns a default (all-absent) ConfigState when the
 * namespace/key doesn't exist yet. loadDeviceBootConfig() initializes `confirmed` to slot A on
 * the very first boot, so subsequent load() calls always see a confirmed slot.
 */
class ConfigStateStore {
public:
    explicit ConfigStateStore(std::shared_ptr<NvsStore> nvs)
        : nvs(std::move(nvs)) {
    }

    ConfigState load() const {
        JsonDocument doc;
        if (!nvs->getJson(KEY, doc)) {
            return {};
        }
        return doc.as<ConfigState>();
    }

    void save(const ConfigState& state) {
        if (!nvs->set(KEY, state)) {
            LOGE("Failed to save config-state");
        }
    }

private:
    static constexpr const char* KEY = "state";

    std::shared_ptr<NvsStore> nvs;
};

}    // namespace cornucopia::ugly_duckling::kernel::config
