#include <catch2/catch_test_macros.hpp>

#include <ConfigBootPlan.hpp>

using namespace cornucopia::ugly_duckling::kernel;

TEST_CASE("decideBootPlan: no config-state at all loads nothing, best-effort") {
    ConfigState state;
    BootPlan plan = decideBootPlan(state);

    REQUIRE_FALSE(plan.slotToLoad.has_value());
    REQUIRE_FALSE(plan.strict);
    REQUIRE_FALSE(plan.crashRecoveryCheckpoint.has_value());
}

TEST_CASE("decideBootPlan: confirmed only, no requested, loads confirmed best-effort") {
    ConfigState state { .confirmed = ConfigSlot::A };
    BootPlan plan = decideBootPlan(state);

    REQUIRE(plan.slotToLoad == ConfigSlot::A);
    REQUIRE_FALSE(plan.strict);
    REQUIRE_FALSE(plan.crashRecoveryCheckpoint.has_value());
}

TEST_CASE("decideBootPlan: pending requested is loaded strictly and marked attempted before load") {
    ConfigState state {
        .confirmed = ConfigSlot::A,
        .requested = RequestedConfig { .slot = ConfigSlot::B, .status = RequestedConfigStatus::Pending },
    };
    BootPlan plan = decideBootPlan(state);

    REQUIRE(plan.slotToLoad == ConfigSlot::B);
    REQUIRE(plan.strict);
    REQUIRE(plan.crashRecoveryCheckpoint.has_value());
    REQUIRE(plan.crashRecoveryCheckpoint->confirmed == ConfigSlot::A);
    REQUIRE(plan.crashRecoveryCheckpoint->requested->slot == ConfigSlot::B);
    REQUIRE(plan.crashRecoveryCheckpoint->requested->status == RequestedConfigStatus::Attempted);
}

TEST_CASE("decideBootPlan: pending requested with no confirmed slot yet (first-ever requested set)") {
    ConfigState state {
        .requested = RequestedConfig { .slot = ConfigSlot::B, .status = RequestedConfigStatus::Pending },
    };
    BootPlan plan = decideBootPlan(state);

    REQUIRE(plan.slotToLoad == ConfigSlot::B);
    REQUIRE(plan.strict);
    REQUIRE_FALSE(plan.crashRecoveryCheckpoint->confirmed.has_value());
}

TEST_CASE("decideBootPlan: attempted requested (crash mid-boot) reverts to confirmed and records a rejection") {
    ConfigState state {
        .confirmed = ConfigSlot::A,
        .requested = RequestedConfig { .slot = ConfigSlot::B, .status = RequestedConfigStatus::Attempted },
    };
    BootPlan plan = decideBootPlan(state);

    REQUIRE(plan.slotToLoad == ConfigSlot::A);
    REQUIRE_FALSE(plan.strict);
    REQUIRE(plan.crashRecoveryCheckpoint.has_value());
    REQUIRE_FALSE(plan.crashRecoveryCheckpoint->requested.has_value());
    REQUIRE(plan.crashRecoveryCheckpoint->confirmed == ConfigSlot::A);
    REQUIRE(plan.crashRecoveryCheckpoint->rejection == RejectionCode::Internal);
}

TEST_CASE("decideBootPlan: rejected requested (revert cleanup didn't finish) is cleaned up the same way") {
    ConfigState state {
        .confirmed = ConfigSlot::A,
        .requested = RequestedConfig { .slot = ConfigSlot::B, .status = RequestedConfigStatus::Rejected },
    };
    BootPlan plan = decideBootPlan(state);

    REQUIRE(plan.slotToLoad == ConfigSlot::A);
    REQUIRE_FALSE(plan.strict);
    REQUIRE_FALSE(plan.crashRecoveryCheckpoint->requested.has_value());
    REQUIRE(plan.crashRecoveryCheckpoint->rejection == RejectionCode::Internal);
}

TEST_CASE("decideBootPlan: attempted requested with no confirmed slot reverts to no confirmed slot") {
    ConfigState state {
        .requested = RequestedConfig { .slot = ConfigSlot::B, .status = RequestedConfigStatus::Attempted },
    };
    BootPlan plan = decideBootPlan(state);

    REQUIRE_FALSE(plan.slotToLoad.has_value());
    REQUIRE_FALSE(plan.strict);
}

TEST_CASE("decideBootPlan: an already-recorded rejection is never clobbered by a later revert") {
    ConfigState state {
        .confirmed = ConfigSlot::A,
        .requested = RequestedConfig { .slot = ConfigSlot::B, .status = RequestedConfigStatus::Attempted },
        .rejection = RejectionCode::InvalidArgument,
    };
    BootPlan plan = decideBootPlan(state);

    REQUIRE(plan.crashRecoveryCheckpoint->rejection == RejectionCode::InvalidArgument);
}

TEST_CASE("recordStrictBootOutcome: success commits confirmed to the loaded slot and clears requested") {
    ConfigState state {
        .confirmed = ConfigSlot::A,
        .requested = RequestedConfig { .slot = ConfigSlot::B, .status = RequestedConfigStatus::Attempted },
    };

    ConfigState next = recordStrictBootOutcome(state, ConfigSlot::B, true, RejectionCode::Internal);

    REQUIRE(next.confirmed == ConfigSlot::B);
    REQUIRE_FALSE(next.requested.has_value());
}

TEST_CASE("recordStrictBootOutcome: success leaves an unrelated unreported rejection untouched") {
    ConfigState state {
        .confirmed = ConfigSlot::A,
        .requested = RequestedConfig { .slot = ConfigSlot::B, .status = RequestedConfigStatus::Attempted },
        .rejection = RejectionCode::FailedPrecondition,
    };

    ConfigState next = recordStrictBootOutcome(state, ConfigSlot::B, true, RejectionCode::Internal);

    REQUIRE(next.rejection == RejectionCode::FailedPrecondition);
}

TEST_CASE("recordStrictBootOutcome: failure marks requested rejected and records the failure code") {
    ConfigState state {
        .confirmed = ConfigSlot::A,
        .requested = RequestedConfig { .slot = ConfigSlot::B, .status = RequestedConfigStatus::Attempted },
    };

    ConfigState next = recordStrictBootOutcome(state, ConfigSlot::B, false, RejectionCode::Internal);

    REQUIRE(next.confirmed == ConfigSlot::A);
    REQUIRE(next.requested->slot == ConfigSlot::B);
    REQUIRE(next.requested->status == RequestedConfigStatus::Rejected);
    REQUIRE(next.rejection == RejectionCode::Internal);
}
