#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <string>

#include <nvs_flash.h>

#include <ConfigEnvelope.hpp>
#include <StoredConfig.hpp>

using namespace cornucopia::ugly_duckling::kernel;

namespace {

void ensureNvsFlashInitialized() {
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);
}

}    // namespace

TEST_CASE("stored config has no value before anything is stored") {
    ensureNvsFlashInitialized();
    auto nvs = std::make_shared<NvsStore>("stored-config-test");
    nvs->eraseAll();

    StoredConfig config(nvs, "device");
    REQUIRE(!config.hasValue());
}

TEST_CASE("stored config persists and reloads a verbatim envelope across instances") {
    ensureNvsFlashInitialized();
    auto nvs = std::make_shared<NvsStore>("stored-config-test");
    nvs->eraseAll();

    JsonDocument body;
    body["publishInterval"] = 60;

    {
        StoredConfig config(nvs, "device");
        config.store(ConfigEnvelope(body.as<JsonVariantConst>(), "fp-1", "2026-07-30T12:00:00Z"));
        REQUIRE(config.hasValue());
        REQUIRE(config.fingerprint() == "fp-1");
        REQUIRE(config.requestedAt() == "2026-07-30T12:00:00Z");
        REQUIRE(config.data()["publishInterval"].as<int>() == 60);
    }

    // A fresh StoredConfig instance backed by the same NVS namespace/key simulates what
    // happens across a reboot: the envelope must come back exactly as it was stored.
    StoredConfig reloaded(nvs, "device");
    REQUIRE(reloaded.hasValue());
    REQUIRE(reloaded.fingerprint() == "fp-1");
    REQUIRE(reloaded.requestedAt() == "2026-07-30T12:00:00Z");
    REQUIRE(reloaded.data()["publishInterval"].as<int>() == 60);
}

TEST_CASE("storing a new envelope overwrites the previous one") {
    ensureNvsFlashInitialized();
    auto nvs = std::make_shared<NvsStore>("stored-config-test");
    nvs->eraseAll();

    JsonDocument bodyV1;
    bodyV1["publishInterval"] = 60;
    JsonDocument bodyV2;
    bodyV2["publishInterval"] = 120;

    StoredConfig config(nvs, "device");
    config.store(ConfigEnvelope(bodyV1.as<JsonVariantConst>(), "fp-1", "2026-07-30T12:00:00Z"));
    config.store(ConfigEnvelope(bodyV2.as<JsonVariantConst>(), "fp-2", "2026-07-30T13:00:00Z"));

    REQUIRE(config.fingerprint() == "fp-2");
    REQUIRE(config.requestedAt() == "2026-07-30T13:00:00Z");
    REQUIRE(config.data()["publishInterval"].as<int>() == 120);

    StoredConfig reloaded(nvs, "device");
    REQUIRE(reloaded.fingerprint() == "fp-2");
    REQUIRE(reloaded.data()["publishInterval"].as<int>() == 120);
}

TEST_CASE("a legacy bare-body blob (no envelope wrapper) is adopted with an empty fingerprint") {
    ensureNvsFlashInitialized();
    auto nvs = std::make_shared<NvsStore>("stored-config-test");
    nvs->eraseAll();

    // Simulate firmware that predates config reconciliation: the configuration body written
    // directly under the key, with no {data, fingerprint, requestedAt} wrapper.
    JsonDocument legacyBody;
    legacyBody["publishInterval"] = 60;
    nvs->setJson("device", legacyBody.as<JsonVariantConst>());

    StoredConfig config(nvs, "device");
    REQUIRE(config.hasValue());
    REQUIRE(config.fingerprint().empty());
    REQUIRE(config.requestedAt().empty());
    REQUIRE(config.data()["publishInterval"].as<int>() == 60);

    // The adoption also normalizes NVS in place, so a subsequent boot loads a proper envelope
    // directly -- the fallback fires at most once per device.
    StoredConfig reloaded(nvs, "device");
    REQUIRE(reloaded.hasValue());
    REQUIRE(reloaded.fingerprint().empty());
    REQUIRE(reloaded.data()["publishInterval"].as<int>() == 60);
}

TEST_CASE("different keys in the same NVS namespace are stored independently") {
    ensureNvsFlashInitialized();
    auto nvs = std::make_shared<NvsStore>("stored-config-test");
    nvs->eraseAll();

    JsonDocument deviceBody;
    deviceBody["publishInterval"] = 60;
    JsonDocument functionBody;
    functionBody["openDuration"] = 30;

    StoredConfig device(nvs, "device");
    device.store(ConfigEnvelope(deviceBody.as<JsonVariantConst>(), "device-fp", "2026-07-30T12:00:00Z"));

    StoredConfig valve1(nvs, "valve1");
    valve1.store(ConfigEnvelope(functionBody.as<JsonVariantConst>(), "valve1-fp", "2026-07-30T12:05:00Z"));

    StoredConfig reloadedDevice(nvs, "device");
    REQUIRE(reloadedDevice.fingerprint() == "device-fp");
    REQUIRE(reloadedDevice.data()["publishInterval"].as<int>() == 60);

    StoredConfig reloadedValve1(nvs, "valve1");
    REQUIRE(reloadedValve1.fingerprint() == "valve1-fp");
    REQUIRE(reloadedValve1.data()["openDuration"].as<int>() == 30);
}
