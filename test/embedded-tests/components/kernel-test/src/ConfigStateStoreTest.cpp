#include <catch2/catch_test_macros.hpp>

#include <memory>

#include <nvs_flash.h>

#include <config/ConfigBootPlan.hpp>
#include <config/ConfigState.hpp>
#include <config/ConfigStateStore.hpp>

using namespace cornucopia::ugly_duckling::kernel;
using namespace cornucopia::ugly_duckling::kernel::config;

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

TEST_CASE("config state loads as all-absent when the namespace doesn't exist yet") {
    ensureNvsFlashInitialized();
    auto nvs = std::make_shared<NvsStore>("cfg-state-test");
    nvs->eraseAll();

    ConfigStateStore store(nvs);
    ConfigState state = store.load();

    REQUIRE_FALSE(state.confirmed.has_value());
    REQUIRE_FALSE(state.requested.has_value());
    REQUIRE_FALSE(state.rejection.has_value());
}

TEST_CASE("config state persists and reloads across instances") {
    ensureNvsFlashInitialized();
    auto nvs = std::make_shared<NvsStore>("cfg-state-test");
    nvs->eraseAll();

    ConfigState state {
        .confirmed = ConfigSlot::A,
        .requested = RequestedConfig { .slot = ConfigSlot::B, .status = RequestedConfigStatus::Pending },
    };

    {
        ConfigStateStore store(nvs);
        store.save(state);
    }

    // A fresh ConfigStateStore instance backed by the same NVS namespace simulates what happens
    // across a reboot -- the state must come back exactly as it was stored.
    ConfigStateStore reloaded(nvs);
    ConfigState loaded = reloaded.load();
    REQUIRE(loaded.confirmed == ConfigSlot::A);
    REQUIRE(loaded.requested->slot == ConfigSlot::B);
    REQUIRE(loaded.requested->status == RequestedConfigStatus::Pending);
    REQUIRE_FALSE(loaded.rejection.has_value());
}

TEST_CASE("boot slot swap: a pending requested set is applied strictly, then committed as confirmed on success") {
    ensureNvsFlashInitialized();
    auto nvs = std::make_shared<NvsStore>("cfg-state-test");
    nvs->eraseAll();

    // Seed a requested set directly into NVS, as an UPDATE handler would have staged it -- this
    // test exercises only the config-state machine, not the full boot/apply path.
    ConfigState seeded {
        .confirmed = ConfigSlot::A,
        .requested = RequestedConfig { .slot = ConfigSlot::B, .status = RequestedConfigStatus::Pending },
    };
    ConfigStateStore store(nvs);
    store.save(seeded);

    // "Boot" #1: decide what to do, and persist the pending -> attempted transition before
    // attempting to load, exactly as startDevice() does.
    ConfigState loaded = ConfigStateStore(nvs).load();
    BootPlan plan = decideBootPlan(loaded);
    REQUIRE(plan.slotToLoad == ConfigSlot::B);
    REQUIRE(plan.strict);
    REQUIRE(plan.crashRecoveryCheckpoint.has_value());
    ConfigStateStore(nvs).save(*plan.crashRecoveryCheckpoint);

    REQUIRE(ConfigStateStore(nvs).load().requested->status == RequestedConfigStatus::Attempted);

    // The requested set applied cleanly: commit.
    ConfigState next = recordStrictBootOutcome(*plan.crashRecoveryCheckpoint, ConfigSlot::B, true, RejectionCode::Internal);
    ConfigStateStore(nvs).save(next);

    ConfigState committed = ConfigStateStore(nvs).load();
    REQUIRE(committed.confirmed == ConfigSlot::B);
    REQUIRE_FALSE(committed.requested.has_value());

    // "Boot" #2: an ordinary reboot now loads the newly-committed slot, best-effort.
    BootPlan nextBootPlan = decideBootPlan(ConfigStateStore(nvs).load());
    REQUIRE(nextBootPlan.slotToLoad == ConfigSlot::B);
    REQUIRE_FALSE(nextBootPlan.strict);
}

TEST_CASE("boot slot swap: a requested set that fails to apply is reverted to confirmed with a rejection") {
    ensureNvsFlashInitialized();
    auto nvs = std::make_shared<NvsStore>("cfg-state-test");
    nvs->eraseAll();

    ConfigState seeded {
        .confirmed = ConfigSlot::A,
        .requested = RequestedConfig { .slot = ConfigSlot::B, .status = RequestedConfigStatus::Pending },
    };
    ConfigStateStore(nvs).save(seeded);

    BootPlan plan = decideBootPlan(ConfigStateStore(nvs).load());
    ConfigStateStore(nvs).save(*plan.crashRecoveryCheckpoint);

    // The requested set failed to apply: revert.
    ConfigState next = recordStrictBootOutcome(*plan.crashRecoveryCheckpoint, ConfigSlot::B, false, RejectionCode::Internal);
    ConfigStateStore(nvs).save(next);

    ConfigState rejected = ConfigStateStore(nvs).load();
    REQUIRE(rejected.confirmed == ConfigSlot::A);
    REQUIRE(rejected.requested->slot == ConfigSlot::B);
    REQUIRE(rejected.requested->status == RequestedConfigStatus::Rejected);
    REQUIRE(rejected.rejection == RejectionCode::Internal);

    // The next boot reverts to `confirmed`, best-effort, and cleans up `requested`.
    BootPlan revertBootPlan = decideBootPlan(rejected);
    REQUIRE(revertBootPlan.slotToLoad == ConfigSlot::A);
    REQUIRE_FALSE(revertBootPlan.strict);
    ConfigStateStore(nvs).save(*revertBootPlan.crashRecoveryCheckpoint);

    ConfigState cleaned = ConfigStateStore(nvs).load();
    REQUIRE_FALSE(cleaned.requested.has_value());
    REQUIRE(cleaned.confirmed == ConfigSlot::A);
    REQUIRE(cleaned.rejection == RejectionCode::Internal);
}

TEST_CASE("boot slot swap: a crash that leaves requested attempted is reverted on the next boot") {
    ensureNvsFlashInitialized();
    auto nvs = std::make_shared<NvsStore>("cfg-state-test");
    nvs->eraseAll();

    // Simulate a device that persisted the pending -> attempted transition, then crashed before
    // reaching recordStrictBootOutcome -- no explicit "crash" is possible in this test session, so
    // this seeds the post-crash state directly, matching this codebase's established stand-in for
    // "across a reboot" (see StoredConfigTest.cpp).
    ConfigState crashed {
        .confirmed = ConfigSlot::A,
        .requested = RequestedConfig { .slot = ConfigSlot::B, .status = RequestedConfigStatus::Attempted },
    };
    ConfigStateStore(nvs).save(crashed);

    BootPlan plan = decideBootPlan(ConfigStateStore(nvs).load());
    REQUIRE(plan.slotToLoad == ConfigSlot::A);
    REQUIRE_FALSE(plan.strict);
    ConfigStateStore(nvs).save(*plan.crashRecoveryCheckpoint);

    ConfigState reverted = ConfigStateStore(nvs).load();
    REQUIRE(reverted.confirmed == ConfigSlot::A);
    REQUIRE_FALSE(reverted.requested.has_value());
    REQUIRE(reverted.rejection == RejectionCode::Internal);
}
