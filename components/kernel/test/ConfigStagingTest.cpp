#include <catch2/catch_test_macros.hpp>

#include <string>
#include <unordered_map>

#include <ConfigStaging.hpp>

using namespace cornucopia::ugly_duckling::kernel;

namespace {

ConfigEnvelope makeEnvelope(const std::string& value, const std::string& fingerprint, const std::string& requestedAt = "2026-07-30T00:00:00Z") {
    JsonDocument doc;
    doc["value"] = value;
    return { doc.as<JsonVariantConst>(), fingerprint, requestedAt };
}

}    // namespace

TEST_CASE("stageDeviceUpdate: no confirmed slot yet stages into slot A") {
    ConfigState state;
    std::unordered_map<std::string, ConfigEnvelope> current;
    std::vector<ChangedConfiguration> changed = { { "device", makeEnvelope("new-device", "fp-device-new") } };

    StagedUpdate staged = stageDeviceUpdate(state, current, changed);

    REQUIRE(staged.slot == ConfigSlot::A);
    REQUIRE(staged.nextState.requested->slot == ConfigSlot::A);
    REQUIRE(staged.nextState.requested->status == RequestedConfigStatus::Pending);
    REQUIRE_FALSE(staged.nextState.confirmed.has_value());
}

TEST_CASE("stageDeviceUpdate: picks the slot that isn't confirmed") {
    ConfigState stateA { .confirmed = ConfigSlot::A };
    StagedUpdate stagedFromA = stageDeviceUpdate(stateA, {}, {});
    REQUIRE(stagedFromA.slot == ConfigSlot::B);

    ConfigState stateB { .confirmed = ConfigSlot::B };
    StagedUpdate stagedFromB = stageDeviceUpdate(stateB, {}, {});
    REQUIRE(stagedFromB.slot == ConfigSlot::A);
}

TEST_CASE("stageDeviceUpdate: copies unchanged entries verbatim and overwrites only the changed ones") {
    ConfigState state { .confirmed = ConfigSlot::A };
    std::unordered_map<std::string, ConfigEnvelope> current = {
        { "device", makeEnvelope("old-device", "fp-device-old") },
        { "valve1", makeEnvelope("old-valve1", "fp-valve1") },
        { "door1", makeEnvelope("old-door1", "fp-door1") },
    };
    std::vector<ChangedConfiguration> changed = {
        { "device", makeEnvelope("new-device", "fp-device-new") },
        { "door1", makeEnvelope("new-door1", "fp-door1-new") },
    };

    StagedUpdate staged = stageDeviceUpdate(state, current, changed);

    REQUIRE(staged.configurations.size() == 3);
    REQUIRE(staged.configurations.at("device").getFingerprint() == "fp-device-new");
    REQUIRE(staged.configurations.at("valve1").getFingerprint() == "fp-valve1");
    REQUIRE(staged.configurations.at("door1").getFingerprint() == "fp-door1-new");
}

TEST_CASE("stageDeviceUpdate: a changed entry not among the current configurations (a brand-new function) is added") {
    ConfigState state { .confirmed = ConfigSlot::A };
    std::unordered_map<std::string, ConfigEnvelope> current = {
        { "device", makeEnvelope("old-device", "fp-device-old") },
    };
    std::vector<ChangedConfiguration> changed = {
        { "device", makeEnvelope("new-device", "fp-device-new") },
        { "valve2", makeEnvelope("brand-new", "fp-valve2") },
    };

    StagedUpdate staged = stageDeviceUpdate(state, current, changed);

    REQUIRE(staged.configurations.size() == 2);
    REQUIRE(staged.configurations.at("valve2").getFingerprint() == "fp-valve2");
}

TEST_CASE("stageDeviceUpdate: leaves confirmed and an unrelated rejection untouched") {
    ConfigState state { .confirmed = ConfigSlot::A, .rejection = RejectionCode::FailedPrecondition };

    StagedUpdate staged = stageDeviceUpdate(state, {}, {});

    REQUIRE(staged.nextState.confirmed == ConfigSlot::A);
    REQUIRE(staged.nextState.rejection == RejectionCode::FailedPrecondition);
}
