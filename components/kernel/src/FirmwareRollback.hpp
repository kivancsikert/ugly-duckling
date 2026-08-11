#pragma once

#include <optional>
#include <string>

#include <esp_ota_ops.h>

#include <Log.hpp>
#include <config/ConfigState.hpp>

namespace cornucopia::ugly_duckling::kernel {

LOGGING_TAG(ROLLBACK, "rollback")

/**
 * @brief Result of checking whether the bootloader rolled back from a failed OTA partition.
 */
struct RollbackDetection {
    config::RejectionCode rejectionCode;

    /**
     * @brief The firmware version baked into the partition that failed to boot.
     *
     * Read via esp_ota_get_partition_description() from the failed partition's image header,
     * which is intact even when the app code on that partition never successfully ran.
     * Used by CrashManager to attribute a crash to the version that actually caused it,
     * rather than the version we rolled back to.
     */
    std::string failedVersion;
};

/**
 * @brief Detects whether the bootloader rolled back from a failed OTA partition and clears the
 * marker so it isn't reported again on subsequent boots.
 *
 * Requires CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE=y. When the bootloader reverts from a
 * PENDING_VERIFY partition (one that reset before esp_ota_mark_app_valid_cancel_rollback()),
 * it marks that partition ABORTED. esp_ota_get_last_invalid_partition() returns that partition
 * until cleared.
 *
 * Called early in startDevice(), alongside performPendingHttpUpdateIfNecessary(). The two
 * cannot co-occur in practice: a failed download never writes a new partition (so there's
 * nothing to roll back from), and a rollback means the download succeeded but the new
 * firmware failed to boot.
 *
 * @return Detection result with rejection code and failed version, or nullopt if no rollback.
 */
inline std::optional<RollbackDetection> detectAndClearRollback(const esp_partition_t* lastInvalid) {
    if (lastInvalid == nullptr) {
        return std::nullopt;
    }

    // A rollback happened — read the failed partition's version before clearing the marker
    esp_app_desc_t desc {};
    esp_err_t err = esp_ota_get_partition_description(lastInvalid, &desc);

    std::string failedVersion;
    if (err == ESP_OK) {
        failedVersion = desc.version;
        LOGTI(ROLLBACK, "Detected rollback from partition '%s' (version %s)",
            lastInvalid->label, failedVersion.c_str());
    } else {
        failedVersion = "unknown";
        LOGTE(ROLLBACK, "Detected rollback from partition '%s' but failed to read its version: %s",
            lastInvalid->label, esp_err_to_name(err));
    }

    // Clear the marker so this rollback isn't reported on every subsequent boot.
    // esp_ota_invalidate_inactive_ota_data_slot() erases the otadata select record for the
    // inactive slot (app partition content untouched), making esp_ota_get_last_invalid_partition()
    // return NULL afterward. ESP-IDF also calls this internally from esp_ota_begin() when a new
    // OTA write starts, but the next OTA attempt could be weeks away.
    err = esp_ota_invalidate_inactive_ota_data_slot();
    if (err != ESP_OK) {
        LOGTE(ROLLBACK, "Failed to invalidate inactive OTA slot: %s (rollback will be re-reported next boot)",
            esp_err_to_name(err));
    }

    return RollbackDetection {
        .rejectionCode = config::RejectionCode::Internal,
        .failedVersion = std::move(failedVersion),
    };
}

inline std::optional<RollbackDetection> detectAndClearRollback() {
    return detectAndClearRollback(esp_ota_get_last_invalid_partition());
}

/**
 * @brief Confirms the currently running firmware as valid, cancelling any pending rollback.
 *
 * Must be called once the device is known to be in a good state — currently at kernelReady.
 * Until this is called, the bootloader considers the running partition PENDING_VERIFY; if the
 * device resets before this call, the bootloader automatically reverts to the other partition.
 *
 * This is deliberately not the strongest signal: kernelReady doesn't require a live MQTT
 * connection, so a firmware with a networking regression would still confirm and never roll
 * back. Closing that gap wants the generalized "soaking" confirm-valid window noted in the
 * firmware-update-via-sync-update spec.
 */
inline void confirmFirmwareValid() {
    esp_err_t err = esp_ota_mark_app_valid_cancel_rollback();
    switch (err) {
        case ESP_OK:
            LOGTI(ROLLBACK, "Firmware confirmed valid");
            break;
        case ESP_ERR_NOT_SUPPORTED:
            // Normal: rollback is not enabled in the bootloader, or the partition is already
            // confirmed (e.g. first boot after a fresh flash, or a non-OTA boot).
            LOGTD(ROLLBACK, "Firmware confirmation not supported (rollback was not enabled previously or firmware was already confirmed)");
            break;
        default:
            LOGTE(ROLLBACK, "Failed to confirm firmware valid: %s", esp_err_to_name(err));
            break;
    }
}

}    // namespace cornucopia::ugly_duckling::kernel
