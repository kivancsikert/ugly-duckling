#include "functions/Function.hpp"
#include "config/ConfigStateStore.hpp"
#include "NvsStore.hpp"
#include "mqtt/MqttRoot.hpp"
#include "Queue.hpp"
#include "mqtt/MqttDriver.hpp"
#include <ConfigUpdate.hpp>
#include <Log.hpp>

#include <exception>
#include <memory>
#include <string>
#include <unordered_map>

#include <FirmwareUpdateDecision.hpp>
#include <HttpUpdate.hpp>

#include <Restart.hpp>
#include <UpdateFilter.hpp>
#include <config/ConfigBootPlan.hpp>
#include <config/ConfigStaging.hpp>
#include <config/ConfigState.hpp>
#include <config/StoredConfig.hpp>

using namespace cornucopia::ugly_duckling::functions;
using namespace cornucopia::ugly_duckling::kernel;
using namespace cornucopia::ugly_duckling::kernel::config;

/**
 * docs/Configuration.md, "BOOT, SYNC, UPDATE" and "Applying a functions-only UPDATE".
 *
 * The confirmed baseline `stageDeviceUpdate()` merges against is re-read fresh from
 * `configStateStore->load()` on every call, not captured once at boot: a functions-only commit
 * earlier in this same boot session moves `confirmed` to a different slot without a reboot, and
 * the next `UPDATE` must see that new location, not a stale one.
 */
ConfigUpdateResult applyConfigUpdate(
    JsonObjectConst configurations,
    const std::string& deviceConfirmedFingerprint,
    const std::shared_ptr<FunctionRegistry>& functionRegistry,
    const std::shared_ptr<ConfigStateStore>& configStateStore) {

    if (configurations.isNull()) {
        LOGD("No 'configurations' in update");
        return ConfigUpdateResult::NoChanges;
    }

    auto manifest = functionRegistry->manifest();
    std::unordered_map<std::string, std::string> heldFingerprints;
    for (const auto& [name, entry] : manifest) {
        heldFingerprints[name] = entry.fingerprint;
    }
    heldFingerprints[DEVICE_CONFIGURATION_NAME] = deviceConfirmedFingerprint;

    FilteredUpdate update = filterUpdate(configurations, heldFingerprints);
    if (update.changed.empty()) {
        LOGD("Ignoring update: nothing differs from what's currently held");
        return ConfigUpdateResult::NoChanges;
    }

    // Gather the full set the device is currently confirmed/running as, so the free slot ends
    // up self-contained: every entry not touched by this UPDATE is copied verbatim, and
    // stageDeviceUpdate() overwrites the rest. A confirmed slot is always fully self-contained
    // (device + every live function) -- reading it is unconditional. With no confirmed slot at
    // all, the baseline is simply empty: the very first UPDATE a fresh device ever gets is
    // necessarily device-changed (no function can exist without a device configuration defining
    // it), so `update.changed` alone already carries everything needed.
    ConfigState state = configStateStore->load();
    std::unordered_map<std::string, ConfigEnvelope> currentConfigurations;
    if (state.confirmed) {
        auto confirmedNvs = std::make_shared<NvsStore>("config-" + toString(*state.confirmed));
        currentConfigurations[DEVICE_CONFIGURATION_NAME] = StoredConfig(confirmedNvs, DEVICE_CONFIGURATION_NAME).configEnvelope();
        for (const auto& [name, entry] : manifest) {
            currentConfigurations[name] = StoredConfig(confirmedNvs, name).configEnvelope();
        }
    }

    StagedUpdate staged = stageDeviceUpdate(state, currentConfigurations, update.changed);
    auto slotNvs = std::make_shared<NvsStore>("config-" + toString(staged.slot));
    for (const auto& [name, envelope] : staged.configurations) {
        // The free slot is whichever one wasn't confirmed -- its previous occupant, from two
        // staged sets ago, may already hold the correct envelope for an entry this UPDATE didn't
        // touch, so skip the write rather than re-persisting something already there.
        storeIfChanged(slotNvs, name, envelope);
    }
    configStateStore->save(staged.nextState);

    if (update.deviceChanged) {
        LOGI("Device configuration changed via update, staged into slot '%s'",
            toString(staged.slot).c_str());
        return ConfigUpdateResult::DeviceChanged;
    }

    // Functions-only: hot-reload instead of rebooting, but through the same
    // pending -> attempted -> commit/reject machinery a device-changed UPDATE uses across a
    // reboot, so a bad function body reverts atomically instead of leaving a half-applied
    // confirmed slot behind.
    ConfigState attempted = staged.nextState;
    // stageDeviceUpdate() unconditionally populates `requested`
    if (!attempted.requested.has_value()) {
        LOGE("BUG: staged update has no requested config");
        return ConfigUpdateResult::FunctionsFailed;
    }
    attempted.requested->status = RequestedConfigStatus::Attempted;
    configStateStore->save(attempted);

    bool success = true;
    for (const auto& entry : update.changed) {
        try {
            functionRegistry->applyLive(entry.name, entry.envelope);
        } catch (const std::exception& e) {
            LOGE("Failed to apply configuration update for '%s': %s", entry.name.c_str(), e.what());
            success = false;
            break;
        }
    }

    // Redundant guard: stageDeviceUpdate() unconditionally populates `requested`, and the
    // has_value() check above already returned FunctionsFailed. Repeated here to silence a
    // GCC 15 false positive (-Wmaybe-uninitialized, https://gcc.gnu.org/bugzilla/show_bug.cgi?id=80635).
    if (!attempted.requested.has_value()) {
        return ConfigUpdateResult::FunctionsFailed;
    }
    ConfigState outcome = recordStrictBootOutcome(attempted, staged.slot, success, RejectionCode::Internal);
    configStateStore->save(outcome);

    if (!success) {
        LOGE("Functions-only update failed to apply; will reboot to revert");
        return ConfigUpdateResult::FunctionsFailed;
    }

    LOGI("Functions-only update applied and committed to slot '%s'", toString(staged.slot).c_str());
    return ConfigUpdateResult::FunctionsApplied;
}

/**
 * Each UPDATE can carry a `configurations` entry (config reconciliation, docs/Configuration.md)
 * and/or a `firmware` entry (firmware reconciliation, docs/specs/firmware-update-via-sync-update.md).
 * The handler processes them in order -- config first, firmware second -- and picks a single
 * terminal action:
 *   - **Firmware present**: `HttpUpdater::startUpdate()` persists the URL and schedules a delayed
 *     reboot, which subsumes any config-driven reboot (device-changed staging or functions-only
 *     revert) that would otherwise need its own `esp_restart()`.
 *   - **No firmware, config needs reboot**: `esp_restart()` directly (device-changed or
 *     functions-only failure).
 *   - **No firmware, functions-only succeeded**: push to `syncTriggerQueue` to re-advertise the
 *     new fingerprints.
 */
void registerUpdateHandler(
    const std::shared_ptr<MqttRoot>& mqttRoot,
    const std::string& deviceConfirmedFingerprint,
    const std::shared_ptr<FunctionRegistry>& functionRegistry,
    const std::shared_ptr<ConfigStateStore>& configStateStore,
    const std::shared_ptr<CopyQueue<bool>>& syncTriggerQueue,
    const std::shared_ptr<NvsStore>& nvs,
    const std::string& firmwareVersion) {
    mqttRoot->subscribe("update", QoS::ExactlyOnce, [deviceConfirmedFingerprint, functionRegistry, configStateStore, syncTriggerQueue, nvs, firmwareVersion](const std::string&, const JsonObject& request) {
        auto firmwareUrl = parseFirmwareUpdate(request["firmware"], firmwareVersion);
        if (firmwareUrl) {
            LOGI("Firmware update available, will download from %s", firmwareUrl->c_str());
        } else if (!request["firmware"].isNull()) {
            LOGD("Firmware entry present but no action needed (malformed or already current)");
        }

        auto configResult = applyConfigUpdate(request["configurations"], deviceConfirmedFingerprint, functionRegistry, configStateStore);

        // Single terminal action: firmware download (with its own delayed reboot) takes priority
        // and subsumes any config-driven reboot; without firmware, the config result decides.
        if (firmwareUrl) {
            HttpUpdater::startUpdate(*firmwareUrl, nvs);
            return;
        }
        switch (configResult) {
            case ConfigUpdateResult::DeviceChanged:
            case ConfigUpdateResult::FunctionsFailed:
                delayedRestart();
                break;
            case ConfigUpdateResult::FunctionsApplied:
                syncTriggerQueue->overwrite(true);
                break;
            case ConfigUpdateResult::NoChanges:
                break;
        }
    });
}
