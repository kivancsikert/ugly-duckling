#pragma once

#include <kernel/Application.hpp>
#include <kernel/PwmManager.hpp>
#include <kernel/drivers/BatteryDriver.hpp>
#include <kernel/drivers/LedDriver.hpp>
#include <version.h>

using namespace farmhub::kernel;
using namespace farmhub::kernel::drivers;

namespace farmhub { namespace devices {

template <typename TDeviceConfiguration>
class Device {
public:
    Device(gpio_num_t statusPin)
        : statusLed("status", statusPin) {
    }

    void begin() {
        application.begin();
    }

public:
    LedDriver statusLed;
    Application<TDeviceConfiguration> application { statusLed };
    PwmManager pwm;
};

template <typename TDeviceConfiguration>
class BatteryPoweredDevice : public Device<TDeviceConfiguration> {
public:
    BatteryPoweredDevice(gpio_num_t statusPin, gpio_num_t batteryPin, float batteryVoltageDividerRatio)
        : Device<TDeviceConfiguration>(statusPin)
        , batteryDriver(batteryPin, batteryVoltageDividerRatio) {
#ifdef FARMHUB_DEBUG
        Device<TDeviceConfiguration>::application.consolePrinter.registerBattery(batteryDriver);
#endif
        Device<TDeviceConfiguration>::application.registerTelemetryProvider("battery", batteryDriver);
    }

public:
    BatteryDriver batteryDriver;
};

}}    // namespace farmhub::devices
