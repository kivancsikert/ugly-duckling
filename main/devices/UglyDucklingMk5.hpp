#pragma once

#include <kernel/FileSystem.hpp>
#include <kernel/Kernel.hpp>
#include <kernel/Service.hpp>
#include <kernel/drivers/BatteryDriver.hpp>
#include <kernel/drivers/Drv8874Driver.hpp>
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

namespace farmhub {
namespace devices {

class Mk5Config
    : public DeviceConfiguration {
public:
    Mk5Config()
        : DeviceConfiguration("mk5") {
    }
};

namespace pins {
static InternalPinPtr BOOT = InternalPin::registerPin("BOOT", GPIO_NUM_0);
static InternalPinPtr BATTERY = InternalPin::registerPin("BATTERY", GPIO_NUM_1);
static InternalPinPtr STATUS = InternalPin::registerPin("STATUS", GPIO_NUM_2);
static InternalPinPtr AIPROPI = InternalPin::registerPin("AIPROPI", GPIO_NUM_4);

static InternalPinPtr IOA1 = InternalPin::registerPin("A1", GPIO_NUM_5);
static InternalPinPtr IOA2 = InternalPin::registerPin("A2", GPIO_NUM_6);
static InternalPinPtr BIPROPI = InternalPin::registerPin("BIPROPI", GPIO_NUM_7);
static InternalPinPtr IOB1 = InternalPin::registerPin("B1", GPIO_NUM_15);
static InternalPinPtr AIN1 = InternalPin::registerPin("AIN1", GPIO_NUM_16);
static InternalPinPtr AIN2 = InternalPin::registerPin("AIN2", GPIO_NUM_17);
static InternalPinPtr BIN1 = InternalPin::registerPin("BIN1", GPIO_NUM_18);
static InternalPinPtr BIN2 = InternalPin::registerPin("BIN2", GPIO_NUM_8);

static InternalPinPtr DMINUS = InternalPin::registerPin("D-", GPIO_NUM_19);
static InternalPinPtr DPLUS = InternalPin::registerPin("D+", GPIO_NUM_20);

static InternalPinPtr IOB2 = InternalPin::registerPin("B2", GPIO_NUM_9);

static InternalPinPtr NSLEEP = InternalPin::registerPin("NSLEEP", GPIO_NUM_10);
static InternalPinPtr NFault = InternalPin::registerPin("NFault", GPIO_NUM_11);
static InternalPinPtr IOC4 = InternalPin::registerPin("C4", GPIO_NUM_12);
static InternalPinPtr IOC3 = InternalPin::registerPin("C3", GPIO_NUM_13);
static InternalPinPtr IOC2 = InternalPin::registerPin("C2", GPIO_NUM_14);
static InternalPinPtr IOC1 = InternalPin::registerPin("C1", GPIO_NUM_21);
static InternalPinPtr IOD4 = InternalPin::registerPin("D4", GPIO_NUM_47);
static InternalPinPtr IOD3 = InternalPin::registerPin("D3", GPIO_NUM_48);

static InternalPinPtr SDA = InternalPin::registerPin("SDA", GPIO_NUM_35);
static InternalPinPtr SCL = InternalPin::registerPin("SCL", GPIO_NUM_36);

static InternalPinPtr IOD1 = InternalPin::registerPin("D1", GPIO_NUM_37);
static InternalPinPtr IOD2 = InternalPin::registerPin("D2", GPIO_NUM_38);

static InternalPinPtr TCK = InternalPin::registerPin("TCK", GPIO_NUM_39);
static InternalPinPtr TDO = InternalPin::registerPin("TDO", GPIO_NUM_40);
static InternalPinPtr TDI = InternalPin::registerPin("TDI", GPIO_NUM_41);
static InternalPinPtr TMS = InternalPin::registerPin("TMS", GPIO_NUM_42);
static InternalPinPtr RXD0 = InternalPin::registerPin("RXD0", GPIO_NUM_44);
static InternalPinPtr TXD0 = InternalPin::registerPin("TXD0", GPIO_NUM_43);
}    // namespace pins

class UglyDucklingMk5 : public DeviceDefinition<Mk5Config> {
public:
    UglyDucklingMk5()
        : DeviceDefinition<Mk5Config>(
            pins::STATUS,
            pins::BOOT) {
    }

    void registerDeviceSpecificPeripheralFactories(PeripheralManager& peripheralManager) override {
        peripheralManager.registerFactory(valveFactory);
        peripheralManager.registerFactory(flowMeterFactory);
        peripheralManager.registerFactory(flowControlFactory);
        peripheralManager.registerFactory(chickenDoorFactory);
    }

    Drv8874Driver motorADriver {
        pwm,
        pins::AIN1,
        pins::AIN2,
        pins::AIPROPI,
        pins::NFault,
        pins::NSLEEP
    };

    Drv8874Driver motorBDriver {
        pwm,
        pins::BIN1,
        pins::BIN2,
        pins::AIPROPI,
        pins::NFault,
        pins::NSLEEP
    };

    const ServiceRef<PwmMotorDriver> motorA { "a", motorADriver };
    const ServiceRef<PwmMotorDriver> motorB { "b", motorBDriver };
    const std::list<ServiceRef<PwmMotorDriver>> motors { motorA, motorB };

    ValveFactory valveFactory { motors, ValveControlStrategyType::Latching };
    FlowMeterFactory flowMeterFactory;
    FlowControlFactory flowControlFactory { motors, ValveControlStrategyType::Latching };
    ChickenDoorFactory chickenDoorFactory { motors };
};

}}    // namespace farmhub::devices
