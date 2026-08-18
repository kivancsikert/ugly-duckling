#pragma once

#include <memory>

#include <KernelStatus.hpp>
#include <drivers/BleDriver.hpp>
#include <drivers/RtcDriver.hpp>
#include <drivers/WiFiDriver.hpp>

#include <NetworkConfig.hpp>

using namespace cornucopia::ugly_duckling::kernel;

/**
 * @brief Creates WiFi + RTC drivers and wires up BLE ↔ WiFi callbacks (time sync, scan
 * requests, credential provisioning, connection control, and status notifications).
 *
 * Returns WiFi and RTC drivers; RTC is also captured by BLE closures so it stays alive as
 * long as BLE does.
 */
struct ConnectivityDrivers {
    std::shared_ptr<WiFiDriver> wifi;
    std::shared_ptr<RtcDriver> rtc;
};

ConnectivityDrivers initConnectivity(
    const std::shared_ptr<ModuleStates>& states,
    const std::shared_ptr<NetworkConfig>& networkConfig,
    const std::shared_ptr<BleDriver>& ble);
