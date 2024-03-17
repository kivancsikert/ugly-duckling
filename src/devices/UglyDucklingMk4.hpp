#pragma once

#include <Arduino.h>

#include <kernel/FileSystem.hpp>
#include <kernel/Kernel.hpp>
#include <kernel/Service.hpp>
#include <kernel/drivers/BatteryDriver.hpp>
#include <kernel/drivers/Drv8801Driver.hpp>
#include <kernel/drivers/LedDriver.hpp>

#include <devices/DeviceDefinition.hpp>

#include <peripherals/chicken_door/ChickenDoor.hpp>
#include <peripherals/flow_control/FlowControl.hpp>
#include <peripherals/flow_meter/FlowMeter.hpp>
#include <peripherals/valve/Valve.hpp>

using namespace farmhub::kernel;
using namespace farmhub::peripherals::chicken_door;
using namespace farmhub::peripherals::flow_control;
using namespace farmhub::peripherals::flow_meter;
using namespace farmhub::peripherals::valve;

namespace farmhub::devices {

class Mk4Config
    : public DeviceConfiguration {
public:
    Mk4Config()
        : DeviceConfiguration("mk4") {
    }
};

class UglyDucklingMk4 : public DeviceDefinition<Mk4Config> {
public:
    UglyDucklingMk4()
        : DeviceDefinition<Mk4Config>(
            // Status LED
            GPIO_NUM_26,
            // Boot pin
            GPIO_NUM_0) {
    }

    void registerDeviceSpecificPeripheralFactories(PeripheralManager& peripheralManager) override {
        peripheralManager.registerFactory(valveFactory);
        peripheralManager.registerFactory(flowMeterFactory);
        peripheralManager.registerFactory(flowControlFactory);
        peripheralManager.registerFactory(chickenDoorFactory);
    }

    std::list<String> getBuiltInPeripherals() override {
        // Device address is 0x44 = 68
        return {
            R"({
                "type": "environment:sht3x",
                "name": "environment",
                "params": {
                    "address": "0x44",
                    "sda": 8,
                    "scl": 9
                }
            })"
        };
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
    const std::list<ServiceRef<PwmMotorDriver>> motors { motor };

    ValveFactory valveFactory { motors, ValveControlStrategyType::NormallyClosed };
    FlowMeterFactory flowMeterFactory;
    FlowControlFactory flowControlFactory { motors, ValveControlStrategyType::NormallyClosed };
    ChickenDoorFactory chickenDoorFactory { motors };
};

}    // namespace farmhub::devices
