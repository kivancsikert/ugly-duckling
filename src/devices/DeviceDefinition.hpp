#pragma once

#include <kernel/Kernel.hpp>
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
        kernel.begin();
    }

public:
    LedDriver statusLed;
    Kernel<TDeviceConfiguration> kernel { statusLed };
    PwmManager pwm;
};

template <typename TDeviceConfiguration>
class BatteryPoweredDeviceDefinition : public DeviceDefinition<TDeviceConfiguration> {
public:
    BatteryPoweredDeviceDefinition(gpio_num_t statusPin, gpio_num_t batteryPin, float batteryVoltageDividerRatio)
        : DeviceDefinition<TDeviceConfiguration>(statusPin)
        , batteryDriver(batteryPin, batteryVoltageDividerRatio) {
#ifdef FARMHUB_DEBUG
        DeviceDefinition<TDeviceConfiguration>::kernel.consolePrinter.registerBattery(batteryDriver);
#endif
        DeviceDefinition<TDeviceConfiguration>::kernel.registerTelemetryProvider("battery", batteryDriver);
    }

public:
    BatteryDriver batteryDriver;
};

}}    // namespace farmhub::devices
