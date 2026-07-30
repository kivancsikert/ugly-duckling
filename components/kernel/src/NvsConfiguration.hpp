#pragma once

#include <functional>
#include <memory>

#include <ArduinoJson.h>

#include "Configuration.hpp"
#include "NvsStore.hpp"

namespace cornucopia::ugly_duckling::kernel {

/**
 * @brief Loads a ConfigurationSection from NVS, and persists updates back to NVS.
 *
 * Parsing always yields a fresh, immutable snapshot: the constructor and update() each
 * build a new TConfiguration rather than mutating one in place. The raw JSON body behind
 * the current snapshot is retained verbatim (getRawJson()) so callers can echo it back
 * without re-serializing through the Configuration/Property tree.
 */
template <std::derived_from<ConfigurationSection> TConfiguration>
class NvsConfiguration {
public:
    NvsConfiguration(std::shared_ptr<NvsStore> nvs, const std::string& key)
        : nvs(std::move(nvs))
        , key(key) {
        auto initial = std::make_shared<TConfiguration>();
        if (this->nvs->getJson(key, raw)) {
            initial->load(raw.as<JsonObject>());
            LOGD("Loaded NVS config for '%s'", key.c_str());
        } else {
            LOGD("No NVS config found for '%s', using defaults", key.c_str());
        }
        config = initial;
    }

    void update(const JsonObject& json) {
        auto updated = std::make_shared<TConfiguration>();
        updated->load(json);
        config = updated;
        raw.set(json);
        if (!nvs->setJson(key, json)) {
            LOGE("Failed to save NVS config for '%s'", key.c_str());
        }
    }

    std::shared_ptr<TConfiguration> getConfig() const {
        return config;
    }

    const JsonDocument& getRawJson() const {
        return raw;
    }

private:
    std::shared_ptr<NvsStore> nvs;
    const std::string key;
    JsonDocument raw;
    std::shared_ptr<TConfiguration> config;
};

/**
 * @brief Loads a ConfigurationSection from NVS by key.
 * Returns default-constructed config if key is absent or cannot be parsed.
 * rawJson is populated with the verbatim JSON body that was parsed, or left null if the
 * key was absent, so callers can echo it back without re-serializing through Configuration.
 */
template <std::derived_from<ConfigurationSection> TConfiguration, typename... TArgs>
std::shared_ptr<TConfiguration> loadConfigFromNvs(const std::shared_ptr<NvsStore>& nvs, const std::string& key, JsonDocument& rawJson, TArgs&&... args) {
    auto config = std::make_shared<TConfiguration>(std::forward<TArgs>(args)...);
    if (nvs->getJson(key, rawJson)) {
        config->load(rawJson.as<JsonObject>());
        LOGD("Loaded NVS config for '%s'", key.c_str());
    } else {
        LOGD("No NVS config found for '%s', using defaults", key.c_str());
    }
    return config;
}

}    // namespace cornucopia::ugly_duckling::kernel
