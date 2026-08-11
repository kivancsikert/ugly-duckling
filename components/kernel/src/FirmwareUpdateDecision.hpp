#pragma once

#include <optional>
#include <string>

#include <ArduinoJson.h>

namespace cornucopia::ugly_duckling::kernel {

/**
 * @brief Result of evaluating a firmware entry from an `UPDATE` message.
 *
 * `std::nullopt` means "nothing to do" — either the entry was absent, malformed, or the version
 * matches what's already running. A value means "download this URL."
 *
 * Pure decision function, no NVS/MQTT — unit-testable natively, mirroring UpdateFilter.hpp.
 */
inline std::optional<std::string> parseFirmwareUpdate(JsonObjectConst firmware, const char* currentVersion) {
    if (firmware.isNull()) {
        return std::nullopt;
    }

    if (!firmware["url"].is<std::string>()) {
        return std::nullopt;
    }
    auto url = firmware["url"].as<std::string>();
    if (url.empty()) {
        return std::nullopt;
    }

    if (!firmware["version"].is<std::string>()) {
        return std::nullopt;
    }
    auto version = firmware["version"].as<std::string>();
    if (version.empty()) {
        return std::nullopt;
    }

    // Already running this version — defensive no-op, mirroring filterUpdate's fingerprint-skip
    if (version == currentVersion) {
        return std::nullopt;
    }

    return url;
}

}    // namespace cornucopia::ugly_duckling::kernel
