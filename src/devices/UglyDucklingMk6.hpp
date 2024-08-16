#pragma once

#include <memory>

#include <kernel/FileSystem.hpp>
#include <kernel/Kernel.hpp>
#include <kernel/Service.hpp>
#include <kernel/drivers/BatteryDriver.hpp>
#include <kernel/drivers/CurrentSenseDriver.hpp>
#include <kernel/drivers/Drv8833Driver.hpp>
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
static gpio_num_t BATTERY = Pin::registerPin("BATTERY", GPIO_NUM_1);
static gpio_num_t STATUS = Pin::registerPin("STATUS", GPIO_NUM_2);
static gpio_num_t STATUS2 = Pin::registerPin("STATUS2", GPIO_NUM_4);

static gpio_num_t IOB1 = Pin::registerPin("B1", GPIO_NUM_5);
static gpio_num_t IOA1 = Pin::registerPin("A1", GPIO_NUM_6);
static gpio_num_t DIPROPI = Pin::registerPin("DIPROPI", GPIO_NUM_7);
static gpio_num_t IOA2 = Pin::registerPin("A2", GPIO_NUM_15);
static gpio_num_t AIN1 = Pin::registerPin("AIN1", GPIO_NUM_16);
static gpio_num_t AIN2 = Pin::registerPin("AIN2", GPIO_NUM_17);
static gpio_num_t BIN2 = Pin::registerPin("BIN2", GPIO_NUM_18);
static gpio_num_t BIN1 = Pin::registerPin("BIN1", GPIO_NUM_8);

static gpio_num_t DMINUS = Pin::registerPin("D-", GPIO_NUM_19);
static gpio_num_t DPLUS = Pin::registerPin("D+", GPIO_NUM_20);

static gpio_num_t LEDA_RED = Pin::registerPin("LEDA_RED", GPIO_NUM_46);
static gpio_num_t LEDA_GREEN = Pin::registerPin("LEDA_GREEN", GPIO_NUM_9);

static gpio_num_t NFault = Pin::registerPin("NFault", GPIO_NUM_11);
static gpio_num_t BTN1 = Pin::registerPin("BTN1", GPIO_NUM_12);
static gpio_num_t BTN2 = Pin::registerPin("BTN2", GPIO_NUM_13);
static gpio_num_t IOC4 = Pin::registerPin("C4", GPIO_NUM_14);
static gpio_num_t IOC3 = Pin::registerPin("C3", GPIO_NUM_21);
static gpio_num_t IOC2 = Pin::registerPin("C2", GPIO_NUM_47);
static gpio_num_t IOC1 = Pin::registerPin("C1", GPIO_NUM_48);
static gpio_num_t IOB2 = Pin::registerPin("B2", GPIO_NUM_45);

static gpio_num_t SDA = Pin::registerPin("SDA", GPIO_NUM_35);
static gpio_num_t SCL = Pin::registerPin("SCL", GPIO_NUM_36);

static gpio_num_t LEDB_GREEN = Pin::registerPin("LEDB_GREEN", GPIO_NUM_37);
static gpio_num_t LEDB_RED = Pin::registerPin("LEDB_RED", GPIO_NUM_38);

static gpio_num_t TCK = Pin::registerPin("TCK", GPIO_NUM_39);
static gpio_num_t TDO = Pin::registerPin("TDO", GPIO_NUM_40);
static gpio_num_t TDI = Pin::registerPin("TDI", GPIO_NUM_41);
static gpio_num_t TMS = Pin::registerPin("TMS", GPIO_NUM_42);
static gpio_num_t RXD0 = Pin::registerPin("RXD0", GPIO_NUM_44);
static gpio_num_t TXD0 = Pin::registerPin("TXD0", GPIO_NUM_43);
}    // namespace pins

class Mk6Config
    : public DeviceConfiguration {
public:
    Mk6Config()
        : DeviceConfiguration("mk6") {
    }

    /**
     * @brief The built-in motor driver's nSLEEP pin can be manually set by a jumper,
     * but can be connected to a GPIO pin, too. Defaults to C2.
     */
    Property<gpio_num_t> motorNSleepPin { this, "motorNSleepPin", pins::IOC2 };
};

class UglyDucklingMk6 : public DeviceDefinition<Mk6Config> {
public:
    UglyDucklingMk6(I2CManager& i2c)
        : DeviceDefinition<Mk6Config>(
              pins::STATUS,
              pins::BOOT) {
        // Switch off strapping pin
        // TODO: Add a LED driver instead
        pinMode(pins::LEDA_RED, OUTPUT);
        digitalWrite(pins::LEDA_RED, HIGH);
    }

    virtual std::shared_ptr<BatteryDriver> createBatteryDriver(I2CManager& i2c) override {
        return std::make_shared<AnalogBatteryDriver>(pins::BATTERY, 1.2424);
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
        pins::AIN1,
        pins::AIN2,
        pins::BIN1,
        pins::BIN2,
        pins::NFault,
        config.motorNSleepPin.get()
    };

    SimpleCurrentSenseDriver currentSense { pins::DIPROPI };
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
