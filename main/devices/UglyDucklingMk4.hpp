#pragma once

#include <Arduino.h>

#include <kernel/FileSystem.hpp>
#include <kernel/Kernel.hpp>
#include <kernel/Service.hpp>
#include <kernel/drivers/BatteryDriver.hpp>
#include <kernel/drivers/Drv8801Driver.hpp>
#include <kernel/drivers/LedDriver.hpp>

#include <peripherals/chicken_door/ChickenDoor.hpp>
#include <peripherals/flow_control/FlowControl.hpp>
#include <peripherals/flow_meter/FlowMeter.hpp>
#include <peripherals/valve/Valve.hpp>

#include <devices/DeviceDefinition.hpp>
#include <devices/Pin.hpp>

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

namespace pins {
static PinPtr BOOT = Pin::registerPin("BOOT", GPIO_NUM_0);
static PinPtr STATUS = Pin::registerPin("STATUS", GPIO_NUM_26);

static PinPtr SOIL_MOISTURE = Pin::registerPin("SOIL_MOISTURE", GPIO_NUM_6);
static PinPtr SOIL_TEMP = Pin::registerPin("SOIL_TEMP", GPIO_NUM_7);

static PinPtr VALVE_EN = Pin::registerPin("VALVE_EN", GPIO_NUM_10);
static PinPtr VALVE_PH = Pin::registerPin("VALVE_PH", GPIO_NUM_11);
static PinPtr VALVE_FAULT = Pin::registerPin("VALVE_FAULT", GPIO_NUM_12);
static PinPtr VALVE_SLEEP = Pin::registerPin("VALVE_SLEEP", GPIO_NUM_13);
static PinPtr VALVE_MODE1 = Pin::registerPin("VALVE_MODE1", GPIO_NUM_14);
static PinPtr VALVE_MODE2 = Pin::registerPin("VALVE_MODE2", GPIO_NUM_15);
static PinPtr VALVE_CURRENT = Pin::registerPin("VALVE_CURRENT", GPIO_NUM_16);
static PinPtr FLOW = Pin::registerPin("FLOW", GPIO_NUM_17);

static PinPtr SDA = Pin::registerPin("SDA", GPIO_NUM_8);
static PinPtr SCL = Pin::registerPin("SCL", GPIO_NUM_9);
static PinPtr RXD0 = Pin::registerPin("RXD0", GPIO_NUM_44);
static PinPtr TXD0 = Pin::registerPin("TXD0", GPIO_NUM_43);
}    // namespace pins

class UglyDucklingMk4 : public DeviceDefinition<Mk4Config> {
public:
    UglyDucklingMk4()
        : DeviceDefinition<Mk4Config>(
            pins::STATUS,
            pins::BOOT) {
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
        pins::VALVE_EN,
        pins::VALVE_PH,
        pins::VALVE_MODE1,
        pins::VALVE_MODE2,
        pins::VALVE_CURRENT,
        pins::VALVE_FAULT,
        pins::VALVE_SLEEP
    };

    const ServiceRef<PwmMotorDriver> motor { "motor", motorDriver };
    const std::list<ServiceRef<PwmMotorDriver>> motors { motor };

    ValveFactory valveFactory { motors, ValveControlStrategyType::NormallyClosed };
    FlowMeterFactory flowMeterFactory;
    FlowControlFactory flowControlFactory { motors, ValveControlStrategyType::NormallyClosed };
    ChickenDoorFactory chickenDoorFactory { motors };
};

}    // namespace farmhub::devices
