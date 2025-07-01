#pragma once

#include <FileSystem.hpp>
#include <Pin.hpp>
#include <drivers/Bq27220Driver.hpp>
#include <drivers/Drv8833Driver.hpp>
#include <drivers/LedDriver.hpp>

#include <peripherals/Peripheral.hpp>
#include <peripherals/chicken_door/ChickenDoor.hpp>
#include <peripherals/flow_control/FlowControl.hpp>
#include <peripherals/flow_meter/FlowMeter.hpp>
#include <peripherals/valve/ValvePeripheral.hpp>

#include <devices/DeviceDefinition.hpp>

using namespace farmhub::kernel;
using namespace farmhub::peripherals::chicken_door;
using namespace farmhub::peripherals::flow_control;
using namespace farmhub::peripherals::flow_meter;
using namespace farmhub::peripherals::valve;

namespace farmhub::devices {

namespace pins {
static const InternalPinPtr BOOT = InternalPin::registerPin("BOOT", GPIO_NUM_0);

static const InternalPinPtr IOA2 = InternalPin::registerPin("A2", GPIO_NUM_1);
static const InternalPinPtr IOA1 = InternalPin::registerPin("A1", GPIO_NUM_2);
static const InternalPinPtr IOA3 = InternalPin::registerPin("A3", GPIO_NUM_3);
static const InternalPinPtr IOB3 = InternalPin::registerPin("B3", GPIO_NUM_4);
static const InternalPinPtr IOB1 = InternalPin::registerPin("B1", GPIO_NUM_5);
static const InternalPinPtr IOB2 = InternalPin::registerPin("B2", GPIO_NUM_6);

// GPIO_NUM_7 is NC

static const InternalPinPtr BAT_GPIO = InternalPin::registerPin("BAT_GPIO", GPIO_NUM_8);

static const InternalPinPtr FSPIHD = InternalPin::registerPin("FSPIHD", GPIO_NUM_9);
static const InternalPinPtr FSPICS0 = InternalPin::registerPin("FSPICS0", GPIO_NUM_10);
static const InternalPinPtr FSPID = InternalPin::registerPin("FSPID", GPIO_NUM_11);
static const InternalPinPtr FSPICLK = InternalPin::registerPin("FSPICLK", GPIO_NUM_12);
static const InternalPinPtr FSPIQ = InternalPin::registerPin("FSPIQ", GPIO_NUM_13);
static const InternalPinPtr FSPIWP = InternalPin::registerPin("FSPIWP", GPIO_NUM_14);

static const InternalPinPtr STATUS = InternalPin::registerPin("STATUS", GPIO_NUM_15);
static const InternalPinPtr LOADEN = InternalPin::registerPin("LOADEN", GPIO_NUM_16);

static const InternalPinPtr SCL = InternalPin::registerPin("SCL", GPIO_NUM_17);
static const InternalPinPtr SDA = InternalPin::registerPin("SDA", GPIO_NUM_18);

static const InternalPinPtr DMINUS = InternalPin::registerPin("D-", GPIO_NUM_19);
static const InternalPinPtr DPLUS = InternalPin::registerPin("D+", GPIO_NUM_20);

static const InternalPinPtr IOX1 = InternalPin::registerPin("X1", GPIO_NUM_21);

// GPIO_NUM_22 to GPIO_NUM_36 are NC

static const InternalPinPtr DBIN1 = InternalPin::registerPin("DBIN1", GPIO_NUM_37);
static const InternalPinPtr DBIN2 = InternalPin::registerPin("DBIN2", GPIO_NUM_38);
static const InternalPinPtr DAIN2 = InternalPin::registerPin("DAIN2", GPIO_NUM_39);
static const InternalPinPtr DAIN1 = InternalPin::registerPin("DAIN1", GPIO_NUM_40);
static const InternalPinPtr DNFault = InternalPin::registerPin("DNFault", GPIO_NUM_41);

// GPIO_NUM_42 is NC

static const InternalPinPtr TXD0 = InternalPin::registerPin("TXD0", GPIO_NUM_43);
static const InternalPinPtr RXD0 = InternalPin::registerPin("RXD0", GPIO_NUM_44);
static const InternalPinPtr IOX2 = InternalPin::registerPin("X2", GPIO_NUM_45);
static const InternalPinPtr STATUS2 = InternalPin::registerPin("STATUS2", GPIO_NUM_46);
static const InternalPinPtr IOB4 = InternalPin::registerPin("B4", GPIO_NUM_47);
static const InternalPinPtr IOA4 = InternalPin::registerPin("A4", GPIO_NUM_48);
}    // namespace pins

class Mk7Settings
    : public DeviceSettings {
public:
    Mk7Settings()
        : DeviceSettings("mk7") {
    }
};

class UglyDucklingMk7 : public DeviceDefinition<Mk7Settings> {
public:
    explicit UglyDucklingMk7()
        : DeviceDefinition(pins::STATUS, pins::BOOT) {
        // Switch off strapping pin
        // TODO: Add a LED driver instead
        pins::STATUS2->pinMode(Pin::Mode::Output);
        pins::STATUS2->digitalWrite(1);
    }

    static std::shared_ptr<BatteryDriver> createBatteryDriver(const std::shared_ptr<I2CManager>& i2c) {
        return std::make_shared<Bq27220Driver>(
            i2c,
            pins::SDA,
            pins::SCL,
            BatteryParameters {
                .maximumVoltage = 4.1,
                .bootThreshold = 3.6,
                .shutdownThreshold = 3.0,
            });
    }

protected:
    void registerDeviceSpecificPeripheralFactories(const std::shared_ptr<PeripheralManager>& peripheralManager, const PeripheralServices& services, const std::shared_ptr<Mk7Settings>& /*settings*/) override {
        auto motorDriver = Drv8833Driver::create(
            services.pwmManager,
            pins::DAIN1,
            pins::DAIN2,
            pins::DBIN1,
            pins::DBIN2,
            pins::DNFault,
            pins::LOADEN);

        std::map<std::string, std::shared_ptr<PwmMotorDriver>> motors = { { "a", motorDriver->getMotorA() }, { "b", motorDriver->getMotorB() } };

        peripheralManager->registerFactory(std::make_unique<ValveFactory>(motors, ValveControlStrategyType::Latching));
        peripheralManager->registerFactory(std::make_unique<FlowMeterFactory>());
        peripheralManager->registerFactory(std::make_unique<FlowControlFactory>(motors, ValveControlStrategyType::Latching));
        peripheralManager->registerFactory(std::make_unique<ChickenDoorFactory>(motors));
    }
};

}    // namespace farmhub::devices
