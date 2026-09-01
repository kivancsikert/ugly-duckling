#pragma once

#include <drivers/RtcDriver.hpp>
#include <mqtt/MqttDriver.hpp>

#include <algorithm>
#include <string>

using namespace cornucopia::ugly_duckling::kernel;
using namespace cornucopia::ugly_duckling::kernel::mqtt;

/**
 * @brief Network configuration: MQTT broker settings, NTP, and device identity.
 *
 * Two shapes exist depending on the migration state:
 *   - **Old** (pre-migration, from NVS `config` namespace): has `instance`/`location`, no `id`.
 *   - **New** (post-migration, via UPDATE):               has `id`,                  no `instance`/`location`.
 *
 * Both parse correctly — missing fields fall back to their defaults. The `id` field drives
 * topic root selection (`getTopicRoot()`), hostname, and MQTT client ID (see `startDevice()`).
 * The `instance` and `location` fields are only used as legacy fallbacks when `id` is absent.
 */
struct NetworkConfig : MqttDriver::Config {
    Property<std::string> id { this, "id" };
    // TODO(legacy-v1-topics): remove instance and location once all devices use id-based topics
    Property<std::string> instance { this, "instance" };
    Property<std::string> location { this, "location" };
    NamedConfigurationEntry<RtcDriver::Config> ntp { this, "ntp" };

    // TODO(legacy-v1-topics): remove fallback and the location/instance fields
    std::string getTopicRoot() const {
        if (!id.get().empty()) {
            return "d/" + id.get();
        }
        return (location.get().empty() ? "" : location.get() + "/") + "devices/ugly-duckling/" + instance.get();
    }

    std::string getHostname() const {
        const auto& idValue = id.get();
        if (!idValue.empty()) {
            return idValue;
        }
        // TODO(legacy-v1-topics): remove instance-based hostname fallback
        std::string hostname = instance.get();
        std::ranges::replace(hostname, ':', '-');
        std::erase(hostname, '?');
        return hostname;
    }
};
