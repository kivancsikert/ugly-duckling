#pragma once

#include <kernel/Application.hpp>
#include <kernel/drivers/LedDriver.hpp>

using namespace farmhub::kernel;
using namespace farmhub::kernel::drivers;

namespace farmhub { namespace devices {

template <typename TDeviceConfiguration>
class Device {
public:
    Device(gpio_num_t statusPin)
        : statusLed("status", statusPin) {
    }

public:
    LedDriver statusLed;
    Application<TDeviceConfiguration> application { statusLed };
};

template <typename TDeviceConfiguration>
class BatteryPoweredDevice : public Device<TDeviceConfiguration> {
public:
    BatteryPoweredDevice(gpio_num_t statusPin, gpio_num_t batteryPin, float batteryVoltageDividerRatio)
        : Device<TDeviceConfiguration>(statusPin)
        , batteryDriver(batteryPin, batteryVoltageDividerRatio) {
        Device<TDeviceConfiguration>::application.registerTelemetryProvider("battery", batteryDriver);
    }

public:
    BatteryDriver batteryDriver;
};

}}    // namespace farmhub::devices
