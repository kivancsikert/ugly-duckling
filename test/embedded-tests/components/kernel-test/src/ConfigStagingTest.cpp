#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <string>
#include <unordered_map>

#include <nvs_flash.h>

#include <ConfigBootPlan.hpp>
#include <ConfigEnvelope.hpp>
#include <ConfigStaging.hpp>
#include <ConfigState.hpp>
#include <ConfigStateStore.hpp>
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

// Exercises the real-NVS half of a device-changed UPDATE that registerUpdateHandler() (Device.hpp)
// performs live: reading the currently-confirmed full set, computing the staged slot via
// stageDeviceUpdate(), and writing it into the free slot's own namespace -- a slot holds the device
// document and every function together, keyed by name, in one "config-<slot>" namespace (there is no
// separate function-cfg namespace) -- so the destination slot is self-contained (an untouched
// function's envelope is copied verbatim) without going through MQTT/Device.hpp, mirroring how
// ConfigStateStoreTest.cpp exercises the boot half.
TEST_CASE("staging a device-changed update into the free slot copies unchanged entries and overwrites the rest") {
    ensureNvsFlashInitialized();

    auto stateNvs = std::make_shared<NvsStore>("stg-state");
    stateNvs->eraseAll();
    auto slotANvs = std::make_shared<NvsStore>("stg-a");
    slotANvs->eraseAll();
    auto slotBNvs = std::make_shared<NvsStore>("stg-b");
    slotBNvs->eraseAll();

    // Seed slot A as the currently-confirmed, currently-running set: device + two functions.
    JsonDocument deviceBodyV1;
    deviceBodyV1["publishInterval"] = 60;
    StoredConfig(slotANvs, DEVICE_CONFIGURATION_NAME).store(ConfigEnvelope(deviceBodyV1.as<JsonVariantConst>(), "device-fp-1", "2026-07-30T12:00:00Z"));

    JsonDocument valve1Body;
    valve1Body["openDuration"] = 30;
    StoredConfig(slotANvs, "valve1").store(ConfigEnvelope(valve1Body.as<JsonVariantConst>(), "valve1-fp", "2026-07-30T12:00:00Z"));

    JsonDocument door1BodyV1;
    door1BodyV1["openDuration"] = 10;
    StoredConfig(slotANvs, "door1").store(ConfigEnvelope(door1BodyV1.as<JsonVariantConst>(), "door1-fp-1", "2026-07-30T12:00:00Z"));

    ConfigStateStore configStateStore(stateNvs);
    configStateStore.save(ConfigState { .confirmed = ConfigSlot::A });

    // An UPDATE arrives changing the device document and door1, but not valve1 -- gather the
    // baseline exactly as registerUpdateHandler() does: read every currently-confirmed entry
    // unconditionally (a confirmed slot is always fully self-contained).
    std::unordered_map<std::string, ConfigEnvelope> currentConfigurations;
    currentConfigurations[DEVICE_CONFIGURATION_NAME] = StoredConfig(slotANvs, DEVICE_CONFIGURATION_NAME).configEnvelope();
    currentConfigurations["valve1"] = StoredConfig(slotANvs, "valve1").configEnvelope();
    currentConfigurations["door1"] = StoredConfig(slotANvs, "door1").configEnvelope();

    JsonDocument deviceBodyV2;
    deviceBodyV2["publishInterval"] = 120;
    JsonDocument door1BodyV2;
    door1BodyV2["openDuration"] = 20;
    std::vector<ChangedConfiguration> changed = {
        { DEVICE_CONFIGURATION_NAME, ConfigEnvelope(deviceBodyV2.as<JsonVariantConst>(), "device-fp-2", "2026-07-30T13:00:00Z") },
        { "door1", ConfigEnvelope(door1BodyV2.as<JsonVariantConst>(), "door1-fp-2", "2026-07-30T13:00:00Z") },
    };

    StagedUpdate staged = stageDeviceUpdate(configStateStore.load(), currentConfigurations, changed);
    REQUIRE(staged.slot == ConfigSlot::B);

    // Persist the staged set into the free slot's namespace, exactly as registerUpdateHandler()
    // does, then mark it requested.
    for (const auto& [name, envelope] : staged.configurations) {
        StoredConfig(slotBNvs, name).store(envelope);
    }
    configStateStore.save(staged.nextState);

    // The free slot is self-contained: the changed entries reflect the update...
    StoredConfig reloadedDevice(slotBNvs, DEVICE_CONFIGURATION_NAME);
    REQUIRE(reloadedDevice.fingerprint() == "device-fp-2");
    REQUIRE(reloadedDevice.data()["publishInterval"].as<int>() == 120);

    StoredConfig reloadedDoor1(slotBNvs, "door1");
    REQUIRE(reloadedDoor1.fingerprint() == "door1-fp-2");
    REQUIRE(reloadedDoor1.data()["openDuration"].as<int>() == 20);

    // ...and the untouched function was copied over verbatim, unchanged.
    StoredConfig reloadedValve1(slotBNvs, "valve1");
    REQUIRE(reloadedValve1.hasValue());
    REQUIRE(reloadedValve1.fingerprint() == "valve1-fp");
    REQUIRE(reloadedValve1.data()["openDuration"].as<int>() == 30);

    // config-state now points requested at the free slot, ready for the next boot to load it
    // strictly (ConfigBootPlanTest.cpp/ConfigStateStoreTest.cpp cover that half in isolation).
    ConfigState reloadedState = ConfigStateStore(stateNvs).load();
    REQUIRE(reloadedState.confirmed == ConfigSlot::A);
    REQUIRE(reloadedState.requested->slot == ConfigSlot::B);
    REQUIRE(reloadedState.requested->status == RequestedConfigStatus::Pending);

    BootPlan plan = decideBootPlan(reloadedState);
    REQUIRE(plan.slotToLoad == ConfigSlot::B);
    REQUIRE(plan.strict);
}

TEST_CASE("staging the very first device-changed update (no confirmed slot yet) picks slot A") {
    ensureNvsFlashInitialized();

    auto stateNvs = std::make_shared<NvsStore>("stg-state");
    stateNvs->eraseAll();
    auto slotANvs = std::make_shared<NvsStore>("stg-a");
    slotANvs->eraseAll();

    // A freshly-minted device (or one migrating from before this firmware) has no config-state
    // namespace at all -- there's no flat/unslotted storage to fall back to.
    ConfigStateStore configStateStore(stateNvs);
    REQUIRE_FALSE(configStateStore.load().confirmed.has_value());

    JsonDocument deviceBody;
    deviceBody["publishInterval"] = 60;
    std::vector<ChangedConfiguration> changed = {
        { DEVICE_CONFIGURATION_NAME, ConfigEnvelope(deviceBody.as<JsonVariantConst>(), "device-fp-1", "2026-07-30T12:00:00Z") },
    };

    StagedUpdate staged = stageDeviceUpdate(configStateStore.load(), {}, changed);
    REQUIRE(staged.slot == ConfigSlot::A);

    StoredConfig(slotANvs, DEVICE_CONFIGURATION_NAME).store(staged.configurations.at(DEVICE_CONFIGURATION_NAME));
    configStateStore.save(staged.nextState);

    ConfigState reloadedState = ConfigStateStore(stateNvs).load();
    REQUIRE_FALSE(reloadedState.confirmed.has_value());
    REQUIRE(reloadedState.requested->slot == ConfigSlot::A);
    REQUIRE(reloadedState.requested->status == RequestedConfigStatus::Pending);
}

// Exercises the real-NVS sequence registerUpdateHandler() runs for a functions-only UPDATE
// (docs/Configuration.md, "Applying a functions-only UPDATE"): stage into the free slot, mark
// attempted, then -- since applying live is FunctionRegistry's job and needs no NVS of its own --
// go straight to the same commit/reject decision a strict boot makes (recordStrictBootOutcome(),
// already covered exhaustively by ConfigBootPlanTest.cpp), reused here for a live-applied outcome
// instead of a boot attempt. No reboot on the happy path: confirmed flips to the new slot in place.
TEST_CASE("a functions-only update that applies cleanly commits without a reboot") {
    ensureNvsFlashInitialized();

    auto stateNvs = std::make_shared<NvsStore>("stg-state");
    stateNvs->eraseAll();
    auto slotANvs = std::make_shared<NvsStore>("stg-a");
    slotANvs->eraseAll();
    auto slotBNvs = std::make_shared<NvsStore>("stg-b");
    slotBNvs->eraseAll();

    JsonDocument deviceBody;
    deviceBody["publishInterval"] = 60;
    StoredConfig(slotANvs, DEVICE_CONFIGURATION_NAME).store(ConfigEnvelope(deviceBody.as<JsonVariantConst>(), "device-fp", "2026-07-30T12:00:00Z"));
    JsonDocument valve1BodyV1;
    valve1BodyV1["openDuration"] = 30;
    StoredConfig(slotANvs, "valve1").store(ConfigEnvelope(valve1BodyV1.as<JsonVariantConst>(), "valve1-fp-1", "2026-07-30T12:00:00Z"));

    ConfigStateStore configStateStore(stateNvs);
    configStateStore.save(ConfigState { .confirmed = ConfigSlot::A });

    std::unordered_map<std::string, ConfigEnvelope> currentConfigurations;
    currentConfigurations[DEVICE_CONFIGURATION_NAME] = StoredConfig(slotANvs, DEVICE_CONFIGURATION_NAME).configEnvelope();
    currentConfigurations["valve1"] = StoredConfig(slotANvs, "valve1").configEnvelope();

    JsonDocument valve1BodyV2;
    valve1BodyV2["openDuration"] = 45;
    std::vector<ChangedConfiguration> changed = {
        { "valve1", ConfigEnvelope(valve1BodyV2.as<JsonVariantConst>(), "valve1-fp-2", "2026-07-30T13:00:00Z") },
    };

    StagedUpdate staged = stageDeviceUpdate(configStateStore.load(), currentConfigurations, changed);
    REQUIRE(staged.slot == ConfigSlot::B);
    for (const auto& [name, envelope] : staged.configurations) {
        StoredConfig(slotBNvs, name).store(envelope);
    }
    configStateStore.save(staged.nextState);

    ConfigState attempted = staged.nextState;
    attempted.requested->status = RequestedConfigStatus::Attempted;
    configStateStore.save(attempted);

    // FunctionRegistry::applyLive() itself needs no NVS (it only updates in-memory tracker state);
    // the persistence already happened above, so this test simulates its success/failure directly.
    ConfigState outcome = recordStrictBootOutcome(attempted, staged.slot, /* success */ true, RejectionCode::Internal);
    configStateStore.save(outcome);

    ConfigState reloadedState = ConfigStateStore(stateNvs).load();
    REQUIRE(reloadedState.confirmed == ConfigSlot::B);
    REQUIRE_FALSE(reloadedState.requested.has_value());

    // The old confirmed slot (A) is untouched -- it's simply the free slot for the next update now.
    StoredConfig oldValve1(slotANvs, "valve1");
    REQUIRE(oldValve1.fingerprint() == "valve1-fp-1");
}

// The failure counterpart: a functions-only update that fails to apply live is marked rejected
// instead of committed, and decideBootPlan() (already covered by ConfigBootPlanTest.cpp) reverts to
// the untouched old confirmed slot on the reboot registerUpdateHandler() triggers.
TEST_CASE("a functions-only update that fails to apply live is rejected, not committed") {
    ensureNvsFlashInitialized();

    auto stateNvs = std::make_shared<NvsStore>("stg-state");
    stateNvs->eraseAll();
    auto slotANvs = std::make_shared<NvsStore>("stg-a");
    slotANvs->eraseAll();
    auto slotBNvs = std::make_shared<NvsStore>("stg-b");
    slotBNvs->eraseAll();

    JsonDocument deviceBody;
    deviceBody["publishInterval"] = 60;
    StoredConfig(slotANvs, DEVICE_CONFIGURATION_NAME).store(ConfigEnvelope(deviceBody.as<JsonVariantConst>(), "device-fp", "2026-07-30T12:00:00Z"));

    ConfigStateStore configStateStore(stateNvs);
    configStateStore.save(ConfigState { .confirmed = ConfigSlot::A });

    std::unordered_map<std::string, ConfigEnvelope> currentConfigurations;
    currentConfigurations[DEVICE_CONFIGURATION_NAME] = StoredConfig(slotANvs, DEVICE_CONFIGURATION_NAME).configEnvelope();

    JsonDocument valveBody;
    valveBody["openDuration"] = 45;
    std::vector<ChangedConfiguration> changed = {
        { "valve1", ConfigEnvelope(valveBody.as<JsonVariantConst>(), "valve1-fp", "2026-07-30T13:00:00Z") },
    };

    StagedUpdate staged = stageDeviceUpdate(configStateStore.load(), currentConfigurations, changed);
    for (const auto& [name, envelope] : staged.configurations) {
        StoredConfig(slotBNvs, name).store(envelope);
    }
    configStateStore.save(staged.nextState);

    ConfigState attempted = staged.nextState;
    attempted.requested->status = RequestedConfigStatus::Attempted;
    configStateStore.save(attempted);

    ConfigState outcome = recordStrictBootOutcome(attempted, staged.slot, /* success */ false, RejectionCode::Internal);
    configStateStore.save(outcome);

    ConfigState reloadedState = ConfigStateStore(stateNvs).load();
    REQUIRE(reloadedState.confirmed == ConfigSlot::A);
    REQUIRE(reloadedState.requested->slot == staged.slot);
    REQUIRE(reloadedState.requested->status == RequestedConfigStatus::Rejected);
    REQUIRE(reloadedState.rejection == RejectionCode::Internal);

    BootPlan plan = decideBootPlan(reloadedState);
    REQUIRE(plan.slotToLoad == ConfigSlot::A);
    REQUIRE_FALSE(plan.strict);
}
