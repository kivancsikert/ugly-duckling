#pragma once

#include <kernel/Application.hpp>
#include <kernel/FileSystem.hpp>
#include <kernel/drivers/BatteryDriver.hpp>
#include <kernel/drivers/Drv8801Driver.hpp>
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

    Drv8801Driver motor {
        pwm,
        GPIO_NUM_10,    // Enable
        GPIO_NUM_11,    // Phase
        GPIO_NUM_14,    // Mode1
        GPIO_NUM_15,    // Mode2
        GPIO_NUM_16,    // Current
        GPIO_NUM_12,    // Fault
        GPIO_NUM_13     // Sleep
    };
};

}}    // namespace farmhub::devices
