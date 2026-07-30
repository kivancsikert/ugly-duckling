#pragma once

#include <memory>
#include <string>
#include <utility>

#include <ArduinoJson.h>

#include <Log.hpp>

#include "ConfigEnvelope.hpp"
#include "NvsStore.hpp"

namespace cornucopia::ugly_duckling::kernel {

/**
 * @brief Owns reading/writing one verbatim {data, fingerprint, requestedAt} envelope in NVS,
 * keyed by name within the given NvsStore namespace. Separate from parsing: this never
 * constructs a Configuration -- parsing data into a typed snapshot is a distinct step. One
 * nvs_set_blob per envelope (via NvsStore's JSON codec) keeps data/fingerprint/requestedAt
 * inseparable, so a crash mid-write can never leave them out of sync with each other.
 *
 * Bridges one legacy shape: firmware that predates config reconciliation stored the
 * configuration body directly under the key, with no wrapper. Such a blob is adopted verbatim
 * as `data`, with an empty fingerprint -- which can never match a real server-issued
 * fingerprint, so the next UPDATE always applies -- and immediately persisted as a proper
 * envelope, so the fallback fires at most once per device. See
 * docs/specs/config-reconciliation.md, "Migration" -- delete once the device-config
 * write-through-NVS path is retired (Phase 3).
 */
class StoredConfig {
public:
    StoredConfig(std::shared_ptr<NvsStore> nvs, const std::string& key)
        : nvs(std::move(nvs))
        , key(key) {
        JsonDocument raw;
        present = this->nvs->getJson(this->key, raw);
        if (!present) {
            LOGD("No config envelope for '%s'", this->key.c_str());
            return;
        }

        auto rawVariant = raw.as<JsonVariantConst>();
        if (rawVariant.is<ConfigEnvelope>()) {
            envelope = rawVariant.as<ConfigEnvelope>();
            LOGD("Loaded config envelope for '%s'", this->key.c_str());
            return;
        }

        LOGD("Adopting legacy bare-body config for '%s'", this->key.c_str());
        store(ConfigEnvelope(rawVariant, "", ""));
    }

    bool hasValue() const {
        return present;
    }

    const JsonDocument& data() const {
        return envelope.getData();
    }

    const std::string& fingerprint() const {
        return envelope.getFingerprint();
    }

    const std::string& requestedAt() const {
        return envelope.getRequestedAt();
    }

    void store(const ConfigEnvelope& updated) {
        if (!nvs->set(key, updated)) {
            LOGE("Failed to save config envelope for '%s'", key.c_str());
            return;
        }
        envelope = updated;
        present = true;
    }

private:
    std::shared_ptr<NvsStore> nvs;
    std::string key;
    ConfigEnvelope envelope;
    bool present;
};

}    // namespace cornucopia::ugly_duckling::kernel
