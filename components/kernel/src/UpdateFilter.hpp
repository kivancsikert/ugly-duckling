#pragma once

#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include <ArduinoJson.h>

#include <config/ConfigEnvelope.hpp>

namespace cornucopia::ugly_duckling::kernel {

using config::ConfigEnvelope;

// Key of the device configuration entry within an `update` message's `configurations` object.
inline const std::string DEVICE_CONFIGURATION_NAME = "device";

struct ChangedConfiguration {
    std::string name;
    ConfigEnvelope envelope;
};

/**
 * @brief Result of filtering an incoming `update` message against the fingerprints the device
 * currently holds (docs/Configuration.md, "BOOT, SYNC, UPDATE").
 * `deviceChanged` reflects whether the "device" entry survived the filter, which is what decides
 * reboot vs. hot-reload.
 */
struct FilteredUpdate {
    std::vector<ChangedConfiguration> changed;
    bool deviceChanged = false;
};

/**
 * @brief Pure fingerprint-skip filtering, decoupled from NVS/MQTT so it is unit-testable natively:
 * for each entry in `configurations`, drop it if its fingerprint matches the one already held under
 * that name in `heldFingerprints`. An unrecognized name (no held fingerprint at all -- a new
 * function, or first-ever device configuration) is always kept. If nothing survives, the whole
 * `update` message is a no-op.
 */
inline FilteredUpdate filterUpdate(JsonObjectConst configurations, const std::unordered_map<std::string, std::string>& heldFingerprints) {
    FilteredUpdate result;
    for (JsonPairConst kv : configurations) {
        std::string name = kv.key().c_str();
        auto envelope = kv.value().as<ConfigEnvelope>();

        auto held = heldFingerprints.find(name);
        if (held != heldFingerprints.end() && held->second == envelope.getFingerprint()) {
            continue;
        }

        if (name == DEVICE_CONFIGURATION_NAME) {
            result.deviceChanged = true;
        }
        result.changed.push_back({ std::move(name), std::move(envelope) });
    }
    return result;
}

}    // namespace cornucopia::ugly_duckling::kernel
