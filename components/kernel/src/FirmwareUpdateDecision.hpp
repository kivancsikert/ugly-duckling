#pragma once

#include <ArduinoJson.h>

#include <optional>
#include <string>

namespace cornucopia::ugly_duckling::kernel {

/**
 * @brief Outcome of the firmware-update decision for an `UPDATE` message.
 *
 * Separates the "what to download" question (`url`) from the "was a download suppressed?"
 * question (`skippedDueToPendingConfig`), so the handler can report the right rejection code
 * without re-deriving the reason.
 */
struct FirmwareUpdateDecision {
    /// Download URL when a firmware update should proceed; `nullopt` otherwise.
    std::optional<std::string> url;
    /// `true` when a valid firmware entry was present but suppressed because the config state
    /// has an unresolved `requested` slot (docs/specs/device-readdressing.md, "Precondition").
    bool skippedDueToPendingConfig = false;
};

/**
 * @brief Result of evaluating a firmware entry from an `UPDATE` message.
 *
 * `std::nullopt` means "nothing to do" — either the entry was absent, malformed, or the version
 * matches what's already running. A value means "download this URL."
 *
 * Pure decision function, no NVS/MQTT — unit-testable natively, mirroring UpdateFilter.hpp.
 */
inline std::optional<std::string> parseFirmwareUpdate(JsonObjectConst firmware, const std::string& currentVersion) {
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

/**
 * @brief Full firmware-update decision: parse the entry, then enforce the "clean config state"
 * precondition (docs/specs/device-readdressing.md, "Precondition").
 *
 * A firmware upgrade is suppressed when `hasRequestedConfig` is true — i.e. the device has a
 * staged config request that hasn't been confirmed or reverted yet. The server is expected to
 * retry once the config settles.
 *
 * Pure decision function — the caller is responsible for reporting the rejection and triggering
 * the appropriate terminal action.
 */
inline FirmwareUpdateDecision decideFirmwareUpdate(JsonObjectConst firmware, const std::string& currentVersion, bool hasRequestedConfig) {
    auto url = parseFirmwareUpdate(firmware, currentVersion);
    if (url && hasRequestedConfig) {
        return { .url = std::nullopt, .skippedDueToPendingConfig = true };
    }
    return { .url = url, .skippedDueToPendingConfig = false };
}

}    // namespace cornucopia::ugly_duckling::kernel
