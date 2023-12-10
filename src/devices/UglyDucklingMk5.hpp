#pragma once

#include <kernel/Application.hpp>
#include <kernel/FileSystem.hpp>
#include <kernel/drivers/BatteryDriver.hpp>
#include <kernel/drivers/LedDriver.hpp>

#include <devices/Device.hpp>

using namespace farmhub::kernel;

namespace farmhub { namespace devices {

class Mk5Config
    : public DeviceConfiguration {
public:
    Mk5Config()
        : DeviceConfiguration("mk5") {
    }
};

class UglyDucklingMk5 : public BatteryPoweredDevice<Mk5Config> {
public:
    UglyDucklingMk5()
        : BatteryPoweredDevice(
            // Status LED
            GPIO_NUM_2,
            // Battery
            // TODO Calibrate battery voltage divider ratio
            GPIO_NUM_1, 2.4848) {
    }
};

}}    // namespace farmhub::devices
