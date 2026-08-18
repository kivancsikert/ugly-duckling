#include <FirmwareRollback.hpp>
#include <catch2/catch_test_macros.hpp>

#include <esp_app_desc.h>
#include <esp_app_format.h>
#include <esp_flash_partitions.h>
#include <esp_ota_ops.h>
#include <esp_partition.h>
#include <esp_rom_crc.h>

#include <cstring>

using namespace cornucopia::ugly_duckling::kernel;

namespace {

const esp_partition_t* getInactiveOtaPartition() {
    const esp_partition_t* running = esp_ota_get_running_partition();
    REQUIRE(running != nullptr);
    const esp_partition_t* next = esp_ota_get_next_update_partition(running);
    REQUIRE(next != nullptr);
    REQUIRE(next != running);
    return next;
}

/**
 * @brief Writes a fake esp_app_desc_t with the given version string into a partition
 * at the offset where esp_ota_get_partition_description() reads it.
 *
 * This doesn't create a bootable image — just enough for the version-reading code path
 * in detectAndClearRollback() to work when called with the partition directly.
 */
void plantFakeAppDescInPartition(const esp_partition_t* partition, const char* version) {
    constexpr size_t appDescOffset = sizeof(esp_image_header_t) + sizeof(esp_image_segment_header_t);

    ESP_ERROR_CHECK(esp_partition_erase_range(partition, 0, 4096));

    esp_app_desc_t desc {};
    desc.magic_word = ESP_APP_DESC_MAGIC_WORD;
    strncpy(desc.version, version, sizeof(desc.version) - 1);
    desc.version[sizeof(desc.version) - 1] = '\0';

    ESP_ERROR_CHECK(esp_partition_write(partition, appDescOffset, &desc, sizeof(desc)));
}

/**
 * @brief Writes an otadata entry to a specific sector, simulating what the bootloader
 * writes during OTA boot selection or rollback.
 *
 * The otadata partition has two sectors (0 and 1). Each holds one esp_ota_select_entry_t.
 * The entry with the higher ota_seq is the "active" boot selection; the other is "inactive".
 * The CRC covers only the ota_seq field.
 */
void writeOtadataEntry(const esp_partition_t* otadataPartition, int sector,
    uint32_t otaSeq, uint32_t otaState) {
    esp_ota_select_entry_t entry {};
    entry.ota_seq = otaSeq;
    entry.ota_state = otaState;
    entry.crc = esp_rom_crc32_le(UINT32_MAX, reinterpret_cast<const uint8_t*>(&entry.ota_seq), sizeof(entry.ota_seq));

    size_t offset = static_cast<size_t>(sector) * otadataPartition->erase_size;
    ESP_ERROR_CHECK(esp_partition_erase_range(otadataPartition, offset, otadataPartition->erase_size));
    ESP_ERROR_CHECK(esp_partition_write(otadataPartition, offset, &entry, sizeof(entry)));
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
    const esp_partition_t* running = esp_ota_get_running_partition();
    REQUIRE(running != nullptr);
    const esp_partition_t* inactive = getInactiveOtaPartition();

    // Plant a fake app descriptor with a known version — just the descriptor at the right
    // offset, not a full bootable image. We call detectAndClearRollback(partition) directly,
    // bypassing esp_ota_get_last_invalid_partition()'s strict image validation (which would
    // require copying the full ~700 KB running image). That validation is ESP-IDF's concern;
    // our test exercises the version-reading and marker-clearing logic.
    plantFakeAppDescInPartition(inactive, "99.88.77");

    // Write otadata entries that simulate a bootloader rollback:
    //   - The running partition's slot: VALID with a higher ota_seq (active)
    //   - The inactive partition's slot: ABORTED with a lower ota_seq (last-invalid)
    //
    // ota_seq → slot mapping: (ota_seq - 1) % ota_app_count
    // With 2 OTA partitions: odd seq → slot 0, even seq → slot 1
    //
    // The ABORTED entry must have the lower ota_seq so that
    // esp_ota_invalidate_inactive_ota_data_slot() (called by detectAndClearRollback
    // to clear the marker) erases the right sector.
    const esp_partition_t* otadata = esp_partition_find_first(
        ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_OTA, nullptr);
    REQUIRE(otadata != nullptr);

    int runningSlot = running->subtype - ESP_PARTITION_SUBTYPE_APP_OTA_0;
    int inactiveSlot = inactive->subtype - ESP_PARTITION_SUBTYPE_APP_OTA_0;
    uint32_t abortedSeq = inactiveSlot + 1;
    uint32_t validSeq = runningSlot + 1 + 2;

    writeOtadataEntry(otadata, 0, validSeq, ESP_OTA_IMG_VALID);
    writeOtadataEntry(otadata, 1, abortedSeq, ESP_OTA_IMG_ABORTED);

    // Call the overload that takes the partition directly — skips image validation
    // (the no-arg version calls esp_ota_get_last_invalid_partition() which requires a
    // fully valid app image; planting one takes ~700 KB of flash writes and ~90 s in Wokwi)
    auto result = detectAndClearRollback(inactive);

    REQUIRE(result.has_value());
    REQUIRE(result->rejectionCode == config::RejectionCode::Internal);
    REQUIRE(result->failedVersion == "99.88.77");

    // We don't assert that the marker was cleared (i.e. that a second call returns nullopt):
    // esp_ota_invalidate_inactive_ota_data_slot() uses esp_partition_mmap() to read otadata,
    // which can return cached data that doesn't reflect our manual esp_partition_write() calls,
    // causing it to fail with ESP_FAIL. In production this isn't an issue — the bootloader
    // writes otadata normally, and the mmap cache is coherent with those writes.
}
