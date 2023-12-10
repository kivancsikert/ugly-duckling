#pragma once

#include <kernel/Application.hpp>
#include <kernel/FileSystem.hpp>
#include <kernel/drivers/BatteryDriver.hpp>
#include <kernel/drivers/LedDriver.hpp>

#include <devices/Device.hpp>

using namespace farmhub::kernel;

namespace farmhub { namespace devices {

class Mk4Config
    : public DeviceConfiguration {
public:
    Mk4Config()
        : DeviceConfiguration("mk4") {
    }
};

class UglyDucklingMk4 : public Device<Mk4Config> {
public:
    UglyDucklingMk4()
        : Device(
            // Status LED
            GPIO_NUM_26) {
    }
};

}}    // namespace farmhub::devices
