#pragma once

#include <memory>
#include <utility>

#include <ArduinoJson.h>

#include <Log.hpp>

#include "ConfigState.hpp"
#include "NvsStore.hpp"

namespace cornucopia::ugly_duckling::kernel {

/**
 * @brief Owns reading/writing the device's ConfigState in NVS (docs/Configuration.md,
 * "Storage: envelopes and slots"). load() returns a default (all-absent) ConfigState when the namespace/key
 * doesn't exist yet -- which is what makes a missing `config-state` namespace equivalent to "no
 * confirmed slot" for free, whether because this is a fresh device or because it last booted
 * pre-Phase-3 firmware (see *Migration* -> "A missing/absent confirmed slot is the one bootstrap
 * path": deliberately not a migration of the old single-envelope layout).
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

}    // namespace cornucopia::ugly_duckling::kernel
