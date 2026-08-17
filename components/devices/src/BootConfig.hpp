#pragma once

#include <memory>
#include <string>

#include <NvsStore.hpp>
#include <config/ConfigBootPlan.hpp>
#include <config/ConfigState.hpp>
#include <config/ConfigStateStore.hpp>

#include <devices/DeviceConfiguration.hpp>

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
    std::shared_ptr<NvsStore> deviceConfigNvs;    // null when no confirmed slot
    std::shared_ptr<DeviceConfiguration> deviceConfig;
    std::string confirmedFingerprint;
    std::string confirmedRequestedAt;
};

/**
 * @brief Resolves the boot plan from the persisted config state, applies any crash-recovery
 * checkpoint, and loads the device configuration from the appropriate slot.
 */
DeviceBootConfig loadDeviceBootConfig(const std::shared_ptr<NvsStore>& configNvs);
