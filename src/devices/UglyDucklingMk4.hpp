#pragma once

#include <kernel/FileSystem.hpp>
#include <kernel/Kernel.hpp>
#include <kernel/Service.hpp>
#include <kernel/drivers/BatteryDriver.hpp>
#include <kernel/drivers/Drv8801Driver.hpp>
#include <kernel/drivers/LedDriver.hpp>

#include <devices/DeviceDefinition.hpp>

#include <peripherals/Valve.hpp>

using namespace farmhub::kernel;
using namespace farmhub::peripherals;

namespace farmhub { namespace devices {

class Mk4Config
    : public DeviceConfiguration {
public:
    Mk4Config()
        : DeviceConfiguration("mk4") {
    }
};

class UglyDucklingMk4 : public DeviceDefinition {
public:
    UglyDucklingMk4()
        : DeviceDefinition(
            // Status LED
            GPIO_NUM_26) {
    }

    void registerPeripheralFactories(PeripheralManager& peripheralManager) override {
        peripheralManager.registerFactory(valveFactory);
    }

    Drv8801Driver motorDriver {
        pwm,
        GPIO_NUM_10,    // Enable
        GPIO_NUM_11,    // Phase
        GPIO_NUM_14,    // Mode1
        GPIO_NUM_15,    // Mode2
        GPIO_NUM_16,    // Current
        GPIO_NUM_12,    // Fault
        GPIO_NUM_13     // Sleep
    };

    const ServiceRef<PwmMotorDriver> motor { "motor", motorDriver };

    ValveFactory valveFactory { { motor }, ValveControlStrategyType::Latching };
};

}}    // namespace farmhub::devices
