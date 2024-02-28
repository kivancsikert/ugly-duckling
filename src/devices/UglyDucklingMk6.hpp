#pragma once

#include <kernel/FileSystem.hpp>
#include <kernel/Kernel.hpp>
#include <kernel/Service.hpp>
#include <kernel/drivers/BatteryDriver.hpp>
#include <kernel/drivers/Drv8833Driver.hpp>
#include <kernel/drivers/LedDriver.hpp>

#include <devices/DeviceDefinition.hpp>
#include <devices/Peripheral.hpp>

#include <peripherals/flow_control/FlowControl.hpp>
#include <peripherals/flow_meter/FlowMeter.hpp>
#include <peripherals/valve/Valve.hpp>

using namespace farmhub::kernel;
using namespace farmhub::peripherals::flow_control;
using namespace farmhub::peripherals::flow_meter;
using namespace farmhub::peripherals::valve;

namespace farmhub::devices {

class Mk6Config
    : public DeviceConfiguration {
public:
    Mk6Config()
        : DeviceConfiguration("mk6") {
    }
};

class UglyDucklingMk6 : public BatteryPoweredDeviceDefinition<Mk6Config> {
public:
    UglyDucklingMk6()
        : BatteryPoweredDeviceDefinition<Mk6Config>(
            // Status LED
            GPIO_NUM_2,
            // Boot pin
            GPIO_NUM_0,
            // Battery
            GPIO_NUM_1, 1.2424) {
        // Switch off strapping pin
        // TODO: Add a LED driver instead
        pinMode(GPIO_NUM_46, OUTPUT);
        digitalWrite(GPIO_NUM_46, HIGH);
    }

    void registerDeviceSpecificPeripheralFactories(PeripheralManager& peripheralManager) override {
        peripheralManager.registerFactory(valveFactory);
        peripheralManager.registerFactory(flowMeterFactory);
        peripheralManager.registerFactory(flowControlFactory);
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
    FlowMeterFactory flowMeterFactory;
    FlowControlFactory flowControlFactory { { motorA, motorB }, ValveControlStrategyType::Latching };
};

}    // namespace farmhub::devices
