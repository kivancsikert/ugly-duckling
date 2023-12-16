#pragma once

#include <kernel/Kernel.hpp>
#include <kernel/FileSystem.hpp>
#include <kernel/Service.hpp>
#include <kernel/drivers/BatteryDriver.hpp>
#include <kernel/drivers/Drv8874Driver.hpp>
#include <kernel/drivers/LedDriver.hpp>

#include <devices/DeviceDefinition.hpp>

using namespace farmhub::kernel;

namespace farmhub {
namespace devices {

class Mk5Config
    : public DeviceConfiguration {
public:
    Mk5Config()
        : DeviceConfiguration("mk5") {
    }
};

class UglyDucklingMk5 : public BatteryPoweredDeviceDefinition<Mk5Config> {
public:
    UglyDucklingMk5()
        : BatteryPoweredDeviceDefinition(
            // Status LED
            GPIO_NUM_2,
            // Battery
            // TODO Calibrate battery voltage divider ratio
            GPIO_NUM_1, 2.4848) {
    }

    Drv8874Driver motorADriver {
        pwm,
        GPIO_NUM_16,    // AIN1
        GPIO_NUM_17,    // AIN2
        GPIO_NUM_4,     // AIPROPI
        GPIO_NUM_11,    // NFault
        GPIO_NUM_10     // NSleep
    };

    Drv8874Driver motorBDriver {
        pwm,
        GPIO_NUM_18,    // BIN1
        GPIO_NUM_8,     // BIN2
        GPIO_NUM_7,     // BIPROPI
        GPIO_NUM_11,    // NFault
        GPIO_NUM_10     // NSleep
    };

    const ServiceRef<PwmMotorDriver> motorA { "a", motorADriver };
    const ServiceRef<PwmMotorDriver> motorB { "b", motorBDriver };
};

}}    // namespace farmhub::devices
