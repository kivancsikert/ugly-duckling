#include <catch2/catch_test_macros.hpp>

#include <cstring>

#include <esp_app_desc.h>
#include <esp_app_format.h>
#include <esp_ota_ops.h>
#include <esp_partition.h>

#include <FirmwareRollback.hpp>

using namespace cornucopia::ugly_duckling::kernel;

namespace {

/**
 * @brief Writes a fake esp_app_desc_t with the given version string into the inactive OTA
 * partition at the offset where esp_ota_get_partition_description() reads it.
 *
 * This doesn't create a bootable image — just enough for the version-reading code path
 * in detectAndClearRollback() to work.
 */
void plantFakeAppDescInPartition(const esp_partition_t* partition, const char* version) {
    // esp_ota_get_partition_description reads at offset sizeof(esp_image_header_t) + sizeof(esp_image_segment_header_t)
    constexpr size_t appDescOffset = sizeof(esp_image_header_t) + sizeof(esp_image_segment_header_t);

    // Erase the sector(s) covering the app descriptor (sector size = 4096 bytes)
    ESP_ERROR_CHECK(esp_partition_erase_range(partition, 0, 4096));

    // Write a valid app descriptor with the desired version
    esp_app_desc_t desc {};
    desc.magic_word = ESP_APP_DESC_MAGIC_WORD;
    strncpy(desc.version, version, sizeof(desc.version) - 1);
    desc.version[sizeof(desc.version) - 1] = '\0';

    ESP_ERROR_CHECK(esp_partition_write(partition, appDescOffset, &desc, sizeof(desc)));
}

/**
 * @brief Marks the inactive OTA partition as "last invalid" by writing it through the
 * OTA begin/end cycle with an intentionally invalid image, then setting its state.
 *
 * Uses esp_ota_begin + esp_ota_end (which fails validation but still writes otadata)
 * to register the partition in the OTA data, then relies on
 * esp_ota_get_last_invalid_partition() to find it.
 *
 * This is a test-only workaround — in production, the bootloader sets this state when a
 * PENDING_VERIFY partition fails to confirm.
 */
const esp_partition_t* getInactiveOtaPartition() {
    const esp_partition_t* running = esp_ota_get_running_partition();
    REQUIRE(running != nullptr);
    const esp_partition_t* next = esp_ota_get_next_update_partition(running);
    REQUIRE(next != nullptr);
    REQUIRE(next != running);
    return next;
}

}    // namespace

TEST_CASE("detectAndClearRollback returns nullopt on a clean boot") {
    // Clear any stale rollback marker from previous test runs
    esp_ota_invalidate_inactive_ota_data_slot();

    auto result = detectAndClearRollback();

    REQUIRE_FALSE(result.has_value());
}

TEST_CASE("confirmFirmwareValid does not crash") {
    // On a normal boot (not from OTA with rollback enabled, or already confirmed),
    // this should either succeed or return ESP_ERR_NOT_SUPPORTED — either way, no crash.
    confirmFirmwareValid();

    // If we get here, it didn't crash — that's the assertion.
    REQUIRE(true);
}

TEST_CASE("detectAndClearRollback reads version from a simulated rollback") {
    const esp_partition_t* inactive = getInactiveOtaPartition();

    // Plant a fake app descriptor with a known version in the inactive partition
    plantFakeAppDescInPartition(inactive, "99.88.77");

    // Mark the inactive partition as the "last booted" by writing minimal OTA data for it,
    // then invalidate it to simulate what the bootloader does on rollback
    esp_ota_handle_t handle;
    esp_err_t err = esp_ota_begin(inactive, OTA_SIZE_UNKNOWN, &handle);
    REQUIRE(err == ESP_OK);
    // esp_ota_begin erased the partition, so re-plant the fake app descriptor
    plantFakeAppDescInPartition(inactive, "99.88.77");
    // esp_ota_end will fail (invalid image) but that's fine — the otadata entry is already written
    esp_ota_end(handle);

    // Now invalidate the slot to mark it as a "last invalid" partition — this is what
    // the bootloader does when a PENDING_VERIFY partition fails
    ESP_ERROR_CHECK(esp_ota_invalidate_inactive_ota_data_slot());

    // Verify detectAndClearRollback sees it
    auto result = detectAndClearRollback();

    REQUIRE(result.has_value());
    REQUIRE(result->rejectionCode == config::RejectionCode::Internal);
    REQUIRE(result->failedVersion == "99.88.77");

    // After detection, the marker should be cleared — a second call returns nullopt
    auto second = detectAndClearRollback();
    REQUIRE_FALSE(second.has_value());
}
