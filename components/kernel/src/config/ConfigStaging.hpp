#pragma once

#include "ConfigEnvelope.hpp"
#include "ConfigState.hpp"
#include <UpdateFilter.hpp>

#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace cornucopia::ugly_duckling::kernel::config {

/**
 * @brief What an `UPDATE` stages into the free slot -- device-changed or functions-only alike, both
 * go through the same staging step (docs/Configuration.md, "Applying a functions-only UPDATE"):
 * which slot, the full self-contained envelope set to write there, and the ConfigState update that
 * marks it `requested`. Persisting `configurations` into that slot's NVS namespace and saving
 * `nextState` is the caller's job -- this is pure and NVS-free so the merge/slot-selection logic is
 * unit-testable on its own.
 */
struct StagedUpdate {
    ConfigSlot slot = ConfigSlot::A;
    std::unordered_map<std::string, ConfigEnvelope> configurations;
    ConfigState nextState;
};

/**
 * @brief Computes the free slot (whichever isn't `confirmed`) and the full envelope set to persist
 * there: every entry in `currentConfigurations` (the device plus every live function, as currently
 * confirmed/running) is copied verbatim, then `changed` overwrites the entries the UPDATE actually
 * touched -- so the destination slot ends up self-contained, exactly as a slot must be, without the
 * caller needing to enumerate what *didn't* change.
 * `nextState` marks `requested = {slot, pending}`, leaving `confirmed`/`rejection` untouched;
 * decideBootPlan() on the next boot takes it from there (or, for a functions-only change applied
 * live without a reboot, the caller reaches the same commit/reject decision directly via
 * recordStrictBootOutcome() -- see docs/Configuration.md, "Applying a functions-only UPDATE").
 *
 * @pre `state.confirmed` must be set (guaranteed by loadDeviceBootConfig at boot).
 */
inline StagedUpdate stageDeviceUpdate(
    const ConfigState& state,
    const std::unordered_map<std::string, ConfigEnvelope>& currentConfigurations,
    const std::vector<ChangedConfiguration>& changed) {
    ConfigSlot slot = otherSlot(state.confirmed.value_or(ConfigSlot::A));

    std::unordered_map<std::string, ConfigEnvelope> merged = currentConfigurations;
    for (const auto& entry : changed) {
        merged[entry.name] = entry.envelope;
    }

    ConfigState nextState = state;
    nextState.requested = RequestedConfig { .slot = slot, .status = RequestedConfigStatus::Pending };

    return { .slot = slot, .configurations = std::move(merged), .nextState = nextState };
}

}    // namespace cornucopia::ugly_duckling::kernel::config
