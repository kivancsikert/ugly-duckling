#pragma once

#include <memory>
#include <string>
#include <utility>

#include <ArduinoJson.h>

#include <Log.hpp>

#include "ConfigEnvelope.hpp"
#include <NvsStore.hpp>

namespace cornucopia::ugly_duckling::kernel::config {

/**
 * @brief Owns reading/writing one verbatim {data, fingerprint, requestedAt} envelope in NVS,
 * keyed by name within the given NvsStore namespace. Separate from parsing: this never
 * constructs a Configuration -- parsing data into a typed snapshot is a distinct step. One
 * nvs_set_blob per envelope (via NvsStore's JSON codec) keeps data/fingerprint/requestedAt
 * inseparable, so a crash mid-write can never leave them out of sync with each other.
 * See docs/Configuration.md, "Storage: envelopes and slots".
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

        envelope = raw.as<ConfigEnvelope>();
        LOGD("Loaded config envelope for '%s'", this->key.c_str());
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

    const ConfigEnvelope& configEnvelope() const {
        return envelope;
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

/**
 * @brief Writes `updated` under `key` in `nvs`, but skips the NVS write entirely if what's already
 * there carries the same fingerprint. Slots ping-pong between `a`/`b`, so the free slot's previous
 * occupant already holds the right envelope for anything that hasn't changed across the last two
 * staged sets (docs/Configuration.md, "Storage: envelopes and slots") -- there's no reason to pay a
 * flash write to re-persist an entry that's already correct.
 */
inline void storeIfChanged(const std::shared_ptr<NvsStore>& nvs, const std::string& key, const ConfigEnvelope& updated) {
    StoredConfig existing(nvs, key);
    if (!existing.hasValue() || existing.fingerprint() != updated.getFingerprint()) {
        existing.store(updated);
    }
}

}    // namespace cornucopia::ugly_duckling::kernel::config
