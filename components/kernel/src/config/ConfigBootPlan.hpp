#pragma once

#include "ConfigState.hpp"

#include <optional>

namespace cornucopia::ugly_duckling::kernel::config {

/**
 * @brief What startDevice() should do this boot, and what crash-recovery checkpoint (if any) to
 * persist before attempting to load -- decided purely from the last-persisted ConfigState, with no
 * NVS/MQTT dependency of its own so it is unit-testable independent of real boot
 * (docs/Configuration.md, "The confirmed/requested state machine").
 */
struct BootPlan {
    // nullopt means "no confirmed slot": boot with device/function defaults, exactly as an empty
    // NVS slot does today.
    std::optional<ConfigSlot> slotToLoad;
    // true only when slotToLoad is a `requested` set being attempted for the first time this boot
    // -- any apply error is a detected failure (see recordStrictBootOutcome). false means
    // best-effort.
    bool strict = false;
    // Set only when the state to persist differs from what was loaded. Must be persisted BEFORE
    // attempting the load below, so that a crash during the attempt leaves behind a state the next
    // boot can recognize and recover from: either the pending -> attempted transition (a crash
    // during a strict load is then seen as "attempted", a detected failure) or a revert-cleanup
    // (attempted/rejected -> confirmed, requested dropped -- crashing during the best-effort load
    // that follows is harmless, since this write already landed).
    std::optional<ConfigState> crashRecoveryCheckpoint;
};

/**
 * @brief Implements the state table from docs/Configuration.md, "The confirmed/requested state
 * machine":
 * - no `requested` -> load `confirmed` (or nothing, if absent), best-effort.
 * - `requested` == pending -> mark attempted, load it strictly.
 * - `requested` == attempted (a crash left it applying) or rejected (a previous revert's cleanup
 *   didn't finish) -> both are a detected failure: revert to `confirmed`, drop `requested`, and
 *   record a rejection if one isn't already stored (never clobber an unreported one).
 */
inline BootPlan decideBootPlan(const ConfigState& state) {
    if (!state.requested) {
        return { .slotToLoad = state.confirmed, .strict = false, .crashRecoveryCheckpoint = std::nullopt };
    }

    const RequestedConfig& requested = *state.requested;
    switch (requested.status) {
        case RequestedConfigStatus::Pending: {
            ConfigState next = state;
            next.requested = RequestedConfig { .slot = requested.slot, .status = RequestedConfigStatus::Attempted };
            return { .slotToLoad = requested.slot, .strict = true, .crashRecoveryCheckpoint = next };
        }
        case RequestedConfigStatus::Attempted:
        case RequestedConfigStatus::Rejected: {
            ConfigState next = state;
            if (!next.rejection) {
                next.rejection = RejectionCode::Internal;
            }
            next.requested.reset();
            return { .slotToLoad = state.confirmed, .strict = false, .crashRecoveryCheckpoint = next };
        }
    }
    // Unreachable, but keeps this a well-formed function for compilers that don't see the switch
    // above as exhaustive.
    return { .slotToLoad = state.confirmed, .strict = false, .crashRecoveryCheckpoint = std::nullopt };
}

/**
 * @brief Called after attempting to load+apply `loadedSlot`, whether that attempt was a strict boot
 * or a functions-only UPDATE applied live (docs/Configuration.md, "Applying a functions-only
 * UPDATE") -- the commit/reject decision is identical either way. Implements "commit is a single
 * pointer flip": on success, `confirmed` flips to `loadedSlot` and `requested` is cleared --
 * `rejection` is left untouched, since it may hold an unrelated code from an earlier failed attempt
 * that hasn't been reported yet (report-once, via BOOT and that boot's SYNC, is item 3's job, not
 * this function's). On
 * failure, `requested` is marked rejected (so the next boot's decideBootPlan reverts) and
 * `rejection` records why.
 */
inline ConfigState recordStrictBootOutcome(
    const ConfigState& stateAfterMarkingAttempted,
    ConfigSlot loadedSlot,
    bool success,
    RejectionCode failureCode) {
    ConfigState next = stateAfterMarkingAttempted;
    if (success) {
        next.confirmed = loadedSlot;
        next.requested.reset();
    } else {
        next.requested = RequestedConfig { .slot = loadedSlot, .status = RequestedConfigStatus::Rejected };
        next.rejection = failureCode;
    }
    return next;
}

}    // namespace cornucopia::ugly_duckling::kernel::config
