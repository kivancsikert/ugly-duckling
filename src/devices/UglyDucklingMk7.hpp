#pragma once

#include <kernel/FileSystem.hpp>
#include <kernel/Kernel.hpp>
#include <kernel/Service.hpp>
#include <kernel/drivers/Bq27220Driver.hpp>
#include <kernel/drivers/Drv8833Driver.hpp>
#include <kernel/drivers/Ina219Driver.hpp>
#include <kernel/drivers/LedDriver.hpp>

#include <peripherals/Peripheral.hpp>
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

namespace pins {
static gpio_num_t BOOT = Pin::registerPin("BOOT", GPIO_NUM_0);

static gpio_num_t IOA2 = Pin::registerPin("A2", GPIO_NUM_1);
static gpio_num_t IOA1 = Pin::registerPin("A1", GPIO_NUM_2);
static gpio_num_t IOA3 = Pin::registerPin("A3", GPIO_NUM_3);
static gpio_num_t IOB3 = Pin::registerPin("B3", GPIO_NUM_4);
static gpio_num_t IOB1 = Pin::registerPin("B1", GPIO_NUM_5);
static gpio_num_t IOB2 = Pin::registerPin("B2", GPIO_NUM_6);

// GPIO_NUM_7 is NC

static gpio_num_t BAT_GPIO = Pin::registerPin("BAT_GPIO", GPIO_NUM_8);

static gpio_num_t FSPIHD = Pin::registerPin("FSPIHD", GPIO_NUM_9);
static gpio_num_t FSPICS0 = Pin::registerPin("FSPICS0", GPIO_NUM_10);
static gpio_num_t FSPID = Pin::registerPin("FSPID", GPIO_NUM_11);
static gpio_num_t FSPICLK = Pin::registerPin("FSPICLK", GPIO_NUM_12);
static gpio_num_t FSPIQ = Pin::registerPin("FSPIQ", GPIO_NUM_13);
static gpio_num_t FSPIWP = Pin::registerPin("FSPIWP", GPIO_NUM_14);

static gpio_num_t STATUS = Pin::registerPin("STATUS", GPIO_NUM_15);
static gpio_num_t LOADEN = Pin::registerPin("LOADEN", GPIO_NUM_16);

static gpio_num_t SCL = Pin::registerPin("SCL", GPIO_NUM_17);
static gpio_num_t SDA = Pin::registerPin("SDA", GPIO_NUM_18);

static gpio_num_t DMINUS = Pin::registerPin("D-", GPIO_NUM_19);
static gpio_num_t DPLUS = Pin::registerPin("D+", GPIO_NUM_20);

static gpio_num_t IOX1 = Pin::registerPin("IOX1", GPIO_NUM_21);

// GPIO_NUM_22 to GPIO_NUM_36 are NC

static gpio_num_t DBIN1 = Pin::registerPin("DBIN1", GPIO_NUM_37);
static gpio_num_t DBIN2 = Pin::registerPin("DBIN2", GPIO_NUM_38);
static gpio_num_t DAIN2 = Pin::registerPin("DAIN2", GPIO_NUM_39);
static gpio_num_t DAIN1 = Pin::registerPin("DAIN1", GPIO_NUM_40);
static gpio_num_t DNFault = Pin::registerPin("DNFault", GPIO_NUM_41);

// GPIO_NUM_42 is NC

static gpio_num_t TXD0 = Pin::registerPin("TXD0", GPIO_NUM_43);
static gpio_num_t RXD0 = Pin::registerPin("RXD0", GPIO_NUM_44);
static gpio_num_t IOX2 = Pin::registerPin("IOX2", GPIO_NUM_45);
static gpio_num_t STATUS2 = Pin::registerPin("STATUS2", GPIO_NUM_46);
static gpio_num_t IOB4 = Pin::registerPin("IOB4", GPIO_NUM_47);
static gpio_num_t IOA4 = Pin::registerPin("IOA4", GPIO_NUM_48);
}    // namespace pins

class Mk7Config
    : public DeviceConfiguration {
public:
    Mk7Config()
        : DeviceConfiguration("mk7") {
    }
};

class UglyDucklingMk7 : public DeviceDefinition<Mk7Config> {
public:
    UglyDucklingMk7(I2CManager& i2c)
        : DeviceDefinition<Mk7Config>(
              pins::STATUS,
              pins::BOOT)
        , currentSense(i2c, pins::SDA, pins::SCL) {
    }

    virtual std::shared_ptr<BatteryDriver> createBatteryDriver(I2CManager& i2c) override {
        return std::make_shared<Bq27220Driver>(i2c, pins::SDA, pins::SCL);
    }

    void registerDeviceSpecificPeripheralFactories(PeripheralManager& peripheralManager) override {
        peripheralManager.registerFactory(valveFactory);
        peripheralManager.registerFactory(flowMeterFactory);
        peripheralManager.registerFactory(flowControlFactory);
        peripheralManager.registerFactory(chickenDoorFactory);
    }

    LedDriver secondaryStatusLed { "status-2", pins::STATUS2 };

    Drv8833Driver motorDriver {
        pwm,
        pins::DAIN1,
        pins::DAIN2,
        pins::DBIN1,
        pins::DBIN2,
        pins::DNFault,
        pins::LOADEN,
    };

    Ina219Driver currentSense;
    ExternalCurrentSensingMotorDriver motorADriver { motorDriver.getMotorA(), currentSense };
    ExternalCurrentSensingMotorDriver motorBDriver { motorDriver.getMotorB(), currentSense };

    const ServiceRef<CurrentSensingMotorDriver> motorA { "a", motorADriver };
    const ServiceRef<CurrentSensingMotorDriver> motorB { "b", motorBDriver };
    const ServiceContainer<CurrentSensingMotorDriver> motors { { motorA, motorB } };

    ValveFactory valveFactory { motors, ValveControlStrategyType::Latching };
    FlowMeterFactory flowMeterFactory;
    FlowControlFactory flowControlFactory { motors, ValveControlStrategyType::Latching };
    ChickenDoorFactory chickenDoorFactory { motors };
};

}    // namespace farmhub::devices
