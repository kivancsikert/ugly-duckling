#pragma once

#include <list>

#include <kernel/Application.hpp>
#include <kernel/FileSystem.hpp>
#include <kernel/Service.hpp>
#include <kernel/drivers/BatteryDriver.hpp>
#include <kernel/drivers/Drv8833Driver.hpp>
#include <kernel/drivers/LedDriver.hpp>
#include <kernel/drivers/MotorDriver.hpp>

#include <devices/Device.hpp>

#include <peripherals/Valve.hpp>

using namespace farmhub::kernel;
using namespace farmhub::peripherals;

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
        application.registerFactory(valveFactory);
    }

    LedDriver secondaryStatusLed { "status-2", GPIO_NUM_4 };

    Drv8833Driver motorDriver {
        pwm,
        GPIO_NUM_16,    // AIN1
        GPIO_NUM_17,    // AIN2
        GPIO_NUM_18,    // BIN1
        GPIO_NUM_8,     // BIN2
        GPIO_NUM_11,    // NFault
        GPIO_NUM_NC     // NSleep -- connected to LOADEN manually
    };

    const ServiceRef<PwmMotorDriver> motorA { "a", motorDriver.getMotorA() };
    const ServiceRef<PwmMotorDriver> motorB { "b", motorDriver.getMotorB() };

    ValveFactory valveFactory { { motorA, motorB }, ValveControlStrategyType::Latching };
};

}}    // namespace farmhub::devices
