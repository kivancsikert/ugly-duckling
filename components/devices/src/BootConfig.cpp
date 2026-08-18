#include "NvsStore.hpp"
#include "config/ConfigBootPlan.hpp"
#include "config/ConfigState.hpp"
#include "config/ConfigStateStore.hpp"
#include "devices/DeviceConfiguration.hpp"
#include <BootConfig.hpp>
#include <Log.hpp>
#include <UpdateFilter.hpp>
#include <config/StoredConfig.hpp>

#include <memory>

using namespace cornucopia::ugly_duckling::devices;
using namespace cornucopia::ugly_duckling::kernel;
using namespace cornucopia::ugly_duckling::kernel::config;

/**
 * Two-slot confirmed/requested atomicity (docs/Configuration.md, "The confirmed/requested state
 * machine"). A device-changed UPDATE (registerUpdateHandler) is what populates `requested`,
 * staging a self-contained set into the free slot; the strict/revert machinery here is what
 * boots it. `configStateStore` is a shared_ptr so registerUpdateHandler's subscription closure
 * can load/save it live, long after startDevice()'s locals would otherwise have gone out of
 * scope (startDevice() never returns).
 */
DeviceBootConfig loadDeviceBootConfig() {
    auto configStateNvs = std::make_shared<NvsStore>("config-state");
    auto configStateStore = std::make_shared<ConfigStateStore>(configStateNvs);
    ConfigState configState = configStateStore->load();
    BootPlan bootPlan = decideBootPlan(configState);

    LOGD("Booting from slot '%s', strict: %s, crash recovery checkpoint to persist: %s",
        bootPlan.slotToLoad ? toString(*bootPlan.slotToLoad).c_str() : "(none)",
        bootPlan.strict ? "true" : "false",
        bootPlan.crashRecoveryCheckpoint ? "true" : "false");

    if (bootPlan.crashRecoveryCheckpoint) {
        configStateStore->save(*bootPlan.crashRecoveryCheckpoint);
        configState = *bootPlan.crashRecoveryCheckpoint;
    }

    // Device configuration is stored as a verbatim envelope like any other reconciled
    // configuration (docs/Configuration.md, "Storage: envelopes and slots"), so its fingerprint is
    // available to the `update` handler without recomputing anything. There is no flat/unslotted
    // storage any more: a device with no confirmed slot (a freshly minted device, or one migrating
    // from before this firmware) boots exactly like an empty slot -- defaults, no functions -- and
    // reconciles from scratch via an empty SYNC prompting the server to re-push everything (see
    // docs/specs/done/config-reconciliation.md, "Migration" -> "A missing/absent confirmed slot is the
    // one bootstrap path").
    std::shared_ptr<NvsStore> deviceConfigNvs;
    if (bootPlan.slotToLoad) {
        deviceConfigNvs = std::make_shared<NvsStore>("config-" + toString(*bootPlan.slotToLoad));
    }
    auto deviceConfig = std::make_shared<DeviceConfiguration>();
    std::string confirmedFingerprint;
    std::string confirmedRequestedAt;
    if (deviceConfigNvs) {
        StoredConfig deviceStoredConfig(deviceConfigNvs, DEVICE_CONFIGURATION_NAME);
        if (deviceStoredConfig.hasValue()) {
            JsonDocument deviceConfigRaw = deviceStoredConfig.data();
            deviceConfig->load(deviceConfigRaw.as<JsonObject>());
            confirmedFingerprint = deviceStoredConfig.fingerprint();
            confirmedRequestedAt = deviceStoredConfig.requestedAt();
        }
    }

    return {
        .configStateStore = configStateStore,
        .configState = configState,
        .bootPlan = bootPlan,
        .deviceConfigNvs = deviceConfigNvs,
        .deviceConfig = deviceConfig,
        .confirmedFingerprint = confirmedFingerprint,
        .confirmedRequestedAt = confirmedRequestedAt,
    };
}
