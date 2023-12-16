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
class DeviceDefinition {
public:
    DeviceDefinition(gpio_num_t statusPin)
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
class BatteryPoweredDeviceDefinition : public DeviceDefinition<TDeviceConfiguration> {
public:
    BatteryPoweredDeviceDefinition(gpio_num_t statusPin, gpio_num_t batteryPin, float batteryVoltageDividerRatio)
        : DeviceDefinition<TDeviceConfiguration>(statusPin)
        , batteryDriver(batteryPin, batteryVoltageDividerRatio) {
#ifdef FARMHUB_DEBUG
        DeviceDefinition<TDeviceConfiguration>::application.consolePrinter.registerBattery(batteryDriver);
#endif
        DeviceDefinition<TDeviceConfiguration>::application.registerTelemetryProvider("battery", batteryDriver);
    }

public:
    BatteryDriver batteryDriver;
};

}}    // namespace farmhub::devices
