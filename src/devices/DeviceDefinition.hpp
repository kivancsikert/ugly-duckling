#pragma once

#include <kernel/Kernel.hpp>
#include <kernel/PwmManager.hpp>
#include <kernel/drivers/BatteryDriver.hpp>
#include <kernel/drivers/LedDriver.hpp>
#include <version.h>

using namespace farmhub::kernel;
using namespace farmhub::kernel::drivers;

namespace farmhub { namespace devices {

class DeviceDefinition {
public:
    DeviceDefinition(gpio_num_t statusPin)
        : statusLed("status", statusPin) {
    }

public:
    LedDriver statusLed;
    PwmManager pwm;
};

class BatteryPoweredDeviceDefinition : public DeviceDefinition {
public:
    BatteryPoweredDeviceDefinition(gpio_num_t statusPin, gpio_num_t batteryPin, float batteryVoltageDividerRatio)
        : DeviceDefinition(statusPin)
        , batteryDriver(batteryPin, batteryVoltageDividerRatio) {
    }

public:
    BatteryDriver batteryDriver;
};

}}    // namespace farmhub::devices
