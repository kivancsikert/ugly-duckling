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
 */
class StoredConfig {
public:
    StoredConfig(std::shared_ptr<NvsStore> nvs, const std::string& key)
        : nvs(std::move(nvs))
        , key(key)
        , present(this->nvs->get(this->key, envelope)) {
        LOGD("%s config envelope for '%s'", present ? "Loaded" : "No", this->key.c_str());
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

    void store(JsonVariantConst data, const std::string& fingerprint, const std::string& requestedAt) {
        ConfigEnvelope updated(data, fingerprint, requestedAt);
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
