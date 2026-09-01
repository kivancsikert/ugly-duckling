#pragma once

#include <config/ConfigEnvelope.hpp>

#include <ArduinoJson.h>

#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace cornucopia::ugly_duckling::kernel {

using config::ConfigEnvelope;

// Keys of the device and network configuration entries within an `update` message's
// `configurations` object.
inline const std::string DEVICE_CONFIGURATION_NAME = "device";
inline const std::string NETWORK_CONFIGURATION_NAME = "network";

struct ChangedConfiguration {
    std::string name;
    ConfigEnvelope envelope;
};

/**
 * @brief Result of filtering an incoming `update` message against the fingerprints the device
 * currently holds (docs/Configuration.md, "BOOT, SYNC, UPDATE").
 * `deviceChanged` and `networkChanged` reflect whether the respective entries survived the filter;
 * either one triggers a reboot rather than a hot-reload, since both affect boot-time state (device
 * configuration defines peripherals/functions; network configuration defines MQTT connection
 * parameters).
 */
struct FilteredUpdate {
    std::vector<ChangedConfiguration> changed;
    bool deviceChanged = false;
    bool networkChanged = false;
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
        } else if (name == NETWORK_CONFIGURATION_NAME) {
            result.networkChanged = true;
        }
        result.changed.push_back({ std::move(name), std::move(envelope) });
    }
    return result;
}

}    // namespace cornucopia::ugly_duckling::kernel
