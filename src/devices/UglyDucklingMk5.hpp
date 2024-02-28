#pragma once

#include <kernel/FileSystem.hpp>
#include <kernel/Kernel.hpp>
#include <kernel/Service.hpp>
#include <kernel/drivers/BatteryDriver.hpp>
#include <kernel/drivers/Drv8874Driver.hpp>
#include <kernel/drivers/LedDriver.hpp>

#include <devices/DeviceDefinition.hpp>

#include <peripherals/flow_control/FlowControl.hpp>
#include <peripherals/flow_meter/FlowMeter.hpp>
#include <peripherals/valve/Valve.hpp>

using namespace farmhub::kernel;
using namespace farmhub::peripherals::flow_control;
using namespace farmhub::peripherals::flow_meter;
using namespace farmhub::peripherals::valve;

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
        : BatteryPoweredDeviceDefinition<Mk5Config>(
            // Status LED
            GPIO_NUM_2,
            // Boot pin
            GPIO_NUM_0,
            // Battery
            // TODO Calibrate battery voltage divider ratio
            GPIO_NUM_1, 2.4848) {
    }

    void registerDeviceSpecificPeripheralFactories(PeripheralManager& peripheralManager) override {
        peripheralManager.registerFactory(valveFactory);
        peripheralManager.registerFactory(flowMeterFactory);
        peripheralManager.registerFactory(flowControlFactory);
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

    ValveFactory valveFactory { { motorA, motorB }, ValveControlStrategyType::Latching };
    FlowMeterFactory flowMeterFactory;
    FlowControlFactory flowControlFactory { { motorA, motorB }, ValveControlStrategyType::Latching };
};

}}    // namespace farmhub::devices
