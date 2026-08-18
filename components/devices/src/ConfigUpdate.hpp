#pragma once

#include <cstdint>
#include <memory>
#include <string>

#include <NvsStore.hpp>
#include <Queue.hpp>
#include <config/ConfigStateStore.hpp>
#include <drivers/LedDriver.hpp>
#include <functions/Function.hpp>
#include <mqtt/MqttRoot.hpp>

using namespace cornucopia::ugly_duckling::functions;
using namespace cornucopia::ugly_duckling::kernel;
using namespace cornucopia::ugly_duckling::kernel::config;
using namespace cornucopia::ugly_duckling::kernel::mqtt;

enum class ConfigUpdateResult : std::uint8_t {
    NoChanges,          // configurations absent or nothing differs from what's held
    DeviceChanged,      // staged into free slot, needs reboot to apply
    FunctionsApplied,   // hot-reloaded live, succeeded and committed
    FunctionsFailed,    // hot-reload failed, needs reboot to revert
};

/**
 * @brief Processes the `configurations` half of an `UPDATE` message: filters against held
 * fingerprints, stages into the free slot, then either reboots (device changed) or hot-reloads
 * (functions only). Returns what happened so the caller can decide the terminal action --
 * including whether a firmware download (handled separately) should subsume the reboot.
 */
ConfigUpdateResult applyConfigUpdate(
    JsonObjectConst configurations,
    const std::string& deviceConfirmedFingerprint,
    const std::shared_ptr<FunctionRegistry>& functionRegistry,
    const std::shared_ptr<ConfigStateStore>& configStateStore);

/**
 * @brief Subscribes to `update` (NoRetain, QoS 2) -- the combined config + firmware inbound path.
 */
void registerUpdateHandler(
    const std::shared_ptr<MqttRoot>& mqttRoot,
    const std::string& deviceConfirmedFingerprint,
    const std::shared_ptr<FunctionRegistry>& functionRegistry,
    const std::shared_ptr<ConfigStateStore>& configStateStore,
    const std::shared_ptr<CopyQueue<bool>>& syncTriggerQueue,
    const std::shared_ptr<NvsStore>& nvs,
    const std::string& firmwareVersion);
