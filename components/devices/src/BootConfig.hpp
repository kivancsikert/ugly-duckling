#pragma once

#include <NetworkConfig.hpp>
#include <NvsStore.hpp>
#include <config/ConfigBootPlan.hpp>
#include <config/ConfigManifestEntry.hpp>
#include <config/ConfigState.hpp>
#include <config/ConfigStateStore.hpp>
#include <devices/DeviceConfiguration.hpp>

#include <memory>

using namespace cornucopia::ugly_duckling::devices;
using namespace cornucopia::ugly_duckling::kernel;
using namespace cornucopia::ugly_duckling::kernel::config;

/**
 * @brief Snapshot of the two-slot boot machinery output: which slot to load, whether it's a
 * strict (requested-set) boot, the resolved device configuration, and the fingerprints needed
 * for SYNC manifest reporting (docs/Configuration.md, "The confirmed/requested state machine").
 */
struct DeviceBootConfig {
    std::shared_ptr<ConfigStateStore> configStateStore;
    ConfigState configState;
    BootPlan bootPlan;
    std::shared_ptr<NvsStore> configNvs;
    std::shared_ptr<DeviceConfiguration> deviceConfig;
    ConfigManifestEntry deviceManifestEntry;
    ConfigManifestEntry networkManifestEntry;
};

/**
 * @brief Resolves the boot plan from the persisted config state, applies any crash-recovery
 * checkpoint, and loads the device configuration from the appropriate slot.
 */
DeviceBootConfig loadDeviceBootConfig();

/**
 * @brief Loads network config from the confirmed config slot, performing bootstrap migration
 * from the old NVS `config` namespace if needed.
 *
 * On first boot after a firmware upgrade, the old `network-config` key lives in the NVS `config`
 * namespace. This function migrates it into the confirmed config slot with the sentinel fingerprint
 * `"unsynced"`, so the SYNC/UPDATE protocol can manage it going forward. On every boot, any
 * orphaned `network-config` key in the old namespace is cleaned up (best-effort).
 *
 * See docs/specs/device-readdressing.md, "Bootstrap migration" and "NVS cleanup".
 *
 * @param legacyConfigNvs  NVS store for the legacy `config` namespace
 * @param slotNvs  NVS store for the boot slot (confirmed or to-be-confirmed); null if no slot
 * @param[out] manifestEntry  Populated with the network config's fingerprint/requestedAt for SYNC
 * @return Parsed network configuration (defaults if no config found anywhere)
 */
std::shared_ptr<NetworkConfig> loadNetworkConfig(
    const std::shared_ptr<NvsStore>& legacyConfigNvs,
    const std::shared_ptr<NvsStore>& slotNvs,
    ConfigManifestEntry& manifestEntry);
