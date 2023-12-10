#pragma once

#include <kernel/Application.hpp>
#include <kernel/FileSystem.hpp>
#include <kernel/drivers/BatteryDriver.hpp>
#include <kernel/drivers/LedDriver.hpp>

#include <devices/Device.hpp>

using namespace farmhub::kernel;

namespace farmhub { namespace devices {

class Mk6Config
    : public DeviceConfiguration {
public:
    Mk6Config()
        : DeviceConfiguration("mk6") {
    }
};

class UglyDucklingMk6 : public BatteryPoweredDevice<Mk6Config> {
public:
    UglyDucklingMk6()
        : BatteryPoweredDevice(
            // Status LED
            GPIO_NUM_2,
            // Battery
            GPIO_NUM_1, 1.2424) {
    }

    LedDriver secondaryStatusLed { "status-2", GPIO_NUM_4 };
};

}}    // namespace farmhub::devices
