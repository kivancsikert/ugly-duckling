#pragma once

#include <kernel/FileSystem.hpp>
#include <kernel/Pin.hpp>
#include <kernel/Service.hpp>
#include <kernel/drivers/Bq27220Driver.hpp>
#include <kernel/drivers/Drv8833Driver.hpp>
#include <kernel/drivers/LedDriver.hpp>

#include <peripherals/Peripheral.hpp>
#include <peripherals/chicken_door/ChickenDoor.hpp>
#include <peripherals/flow_control/FlowControl.hpp>
#include <peripherals/flow_meter/FlowMeter.hpp>
#include <peripherals/valve/Valve.hpp>

#include <devices/DeviceDefinition.hpp>

using namespace farmhub::kernel;
using namespace farmhub::peripherals::chicken_door;
using namespace farmhub::peripherals::flow_control;
using namespace farmhub::peripherals::flow_meter;
using namespace farmhub::peripherals::valve;

namespace farmhub::devices {

namespace pins {
static InternalPinPtr BOOT = InternalPin::registerPin("BOOT", GPIO_NUM_0);

static InternalPinPtr IOA2 = InternalPin::registerPin("A2", GPIO_NUM_1);
static InternalPinPtr IOA1 = InternalPin::registerPin("A1", GPIO_NUM_2);
static InternalPinPtr IOA3 = InternalPin::registerPin("A3", GPIO_NUM_3);
static InternalPinPtr IOB3 = InternalPin::registerPin("B3", GPIO_NUM_4);
static InternalPinPtr IOB1 = InternalPin::registerPin("B1", GPIO_NUM_5);
static InternalPinPtr IOB2 = InternalPin::registerPin("B2", GPIO_NUM_6);

// GPIO_NUM_7 is NC

static InternalPinPtr BAT_GPIO = InternalPin::registerPin("BAT_GPIO", GPIO_NUM_8);

static InternalPinPtr FSPIHD = InternalPin::registerPin("FSPIHD", GPIO_NUM_9);
static InternalPinPtr FSPICS0 = InternalPin::registerPin("FSPICS0", GPIO_NUM_10);
static InternalPinPtr FSPID = InternalPin::registerPin("FSPID", GPIO_NUM_11);
static InternalPinPtr FSPICLK = InternalPin::registerPin("FSPICLK", GPIO_NUM_12);
static InternalPinPtr FSPIQ = InternalPin::registerPin("FSPIQ", GPIO_NUM_13);
static InternalPinPtr FSPIWP = InternalPin::registerPin("FSPIWP", GPIO_NUM_14);

static InternalPinPtr STATUS = InternalPin::registerPin("STATUS", GPIO_NUM_15);
static InternalPinPtr LOADEN = InternalPin::registerPin("LOADEN", GPIO_NUM_16);

static InternalPinPtr SCL = InternalPin::registerPin("SCL", GPIO_NUM_17);
static InternalPinPtr SDA = InternalPin::registerPin("SDA", GPIO_NUM_18);

static InternalPinPtr DMINUS = InternalPin::registerPin("D-", GPIO_NUM_19);
static InternalPinPtr DPLUS = InternalPin::registerPin("D+", GPIO_NUM_20);

static InternalPinPtr IOX1 = InternalPin::registerPin("X1", GPIO_NUM_21);

// GPIO_NUM_22 to GPIO_NUM_36 are NC

static InternalPinPtr DBIN1 = InternalPin::registerPin("DBIN1", GPIO_NUM_37);
static InternalPinPtr DBIN2 = InternalPin::registerPin("DBIN2", GPIO_NUM_38);
static InternalPinPtr DAIN2 = InternalPin::registerPin("DAIN2", GPIO_NUM_39);
static InternalPinPtr DAIN1 = InternalPin::registerPin("DAIN1", GPIO_NUM_40);
static InternalPinPtr DNFault = InternalPin::registerPin("DNFault", GPIO_NUM_41);

// GPIO_NUM_42 is NC

static InternalPinPtr TXD0 = InternalPin::registerPin("TXD0", GPIO_NUM_43);
static InternalPinPtr RXD0 = InternalPin::registerPin("RXD0", GPIO_NUM_44);
static InternalPinPtr IOX2 = InternalPin::registerPin("X2", GPIO_NUM_45);
static InternalPinPtr STATUS2 = InternalPin::registerPin("STATUS2", GPIO_NUM_46);
static InternalPinPtr IOB4 = InternalPin::registerPin("B4", GPIO_NUM_47);
static InternalPinPtr IOA4 = InternalPin::registerPin("A4", GPIO_NUM_48);
}    // namespace pins

class Mk7Config
    : public DeviceConfiguration {
public:
    Mk7Config()
        : DeviceConfiguration("mk7") {
    }
};

class UglyDucklingMk7 : public DeviceDefinition {
public:
    UglyDucklingMk7(std::shared_ptr<Mk7Config> config)
        : DeviceDefinition(pins::STATUS, pins::BOOT) {
    }

    static std::shared_ptr<BatteryDriver> createBatteryDriver(std::shared_ptr<I2CManager> i2c) {
        return std::make_shared<Bq27220Driver>(i2c, pins::SDA, pins::SCL, BatteryParameters {
            .maximumVoltage = 4.1,
            .bootThreshold = 3.7,
            .shutdownThreshold = 3.0,
        });
    }

    void registerDeviceSpecificPeripheralFactories(std::shared_ptr<PeripheralManager> peripheralManager) override {
        peripheralManager->registerFactory(std::make_unique<ValveFactory>(motors, ValveControlStrategyType::Latching));
        peripheralManager->registerFactory(std::make_unique<FlowMeterFactory>());
        peripheralManager->registerFactory(std::make_unique<FlowControlFactory>(motors, ValveControlStrategyType::Latching));
        peripheralManager->registerFactory(std::make_unique<ChickenDoorFactory>(motors));
    }

    std::shared_ptr<LedDriver> secondaryStatusLed { std::make_shared<LedDriver>("status-2", pins::STATUS2) };

    Drv8833Driver motorDriver {
        pwm,
        pins::DAIN1,
        pins::DAIN2,
        pins::DBIN1,
        pins::DBIN2,
        pins::DNFault,
        pins::LOADEN,
    };

    const ServiceRef<PwmMotorDriver> motorA { "a", motorDriver.getMotorA() };
    const ServiceRef<PwmMotorDriver> motorB { "b", motorDriver.getMotorB() };
    const std::list<ServiceRef<PwmMotorDriver>> motors { motorA, motorB };
};

}    // namespace farmhub::devices
