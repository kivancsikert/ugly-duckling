#pragma once

#include <FileSystem.hpp>
#include <Pin.hpp>
#include <drivers/BatteryDriver.hpp>
#include <drivers/Drv8874Driver.hpp>
#include <drivers/LedDriver.hpp>

#include <peripherals/chicken_door/ChickenDoor.hpp>
#include <peripherals/flow_meter/FlowMeter.hpp>
#include <peripherals/valve/ValvePeripheral.hpp>

#include <devices/DeviceDefinition.hpp>

using namespace farmhub::kernel;
using namespace farmhub::peripherals::chicken_door;
using namespace farmhub::peripherals::flow_meter;
using namespace farmhub::peripherals::valve;

namespace farmhub::devices {

class Mk5Settings
    : public DeviceSettings {
public:
    Mk5Settings()
        : DeviceSettings("mk5") {
    }
};

namespace pins {
static const InternalPinPtr BOOT = InternalPin::registerPin("BOOT", GPIO_NUM_0);
static const InternalPinPtr BATTERY = InternalPin::registerPin("BATTERY", GPIO_NUM_1);
static const InternalPinPtr STATUS = InternalPin::registerPin("STATUS", GPIO_NUM_2);
static const InternalPinPtr AIPROPI = InternalPin::registerPin("AIPROPI", GPIO_NUM_4);

static const InternalPinPtr IOA1 = InternalPin::registerPin("A1", GPIO_NUM_5);
static const InternalPinPtr IOA2 = InternalPin::registerPin("A2", GPIO_NUM_6);
static const InternalPinPtr BIPROPI = InternalPin::registerPin("BIPROPI", GPIO_NUM_7);
static const InternalPinPtr IOB1 = InternalPin::registerPin("B1", GPIO_NUM_15);
static const InternalPinPtr AIN1 = InternalPin::registerPin("AIN1", GPIO_NUM_16);
static const InternalPinPtr AIN2 = InternalPin::registerPin("AIN2", GPIO_NUM_17);
static const InternalPinPtr BIN1 = InternalPin::registerPin("BIN1", GPIO_NUM_18);
static const InternalPinPtr BIN2 = InternalPin::registerPin("BIN2", GPIO_NUM_8);

static const InternalPinPtr DMINUS = InternalPin::registerPin("D-", GPIO_NUM_19);
static const InternalPinPtr DPLUS = InternalPin::registerPin("D+", GPIO_NUM_20);

static const InternalPinPtr IOB2 = InternalPin::registerPin("B2", GPIO_NUM_9);

static const InternalPinPtr NSLEEP = InternalPin::registerPin("NSLEEP", GPIO_NUM_10);
static const InternalPinPtr NFault = InternalPin::registerPin("NFault", GPIO_NUM_11);
static const InternalPinPtr IOC4 = InternalPin::registerPin("C4", GPIO_NUM_12);
static const InternalPinPtr IOC3 = InternalPin::registerPin("C3", GPIO_NUM_13);
static const InternalPinPtr IOC2 = InternalPin::registerPin("C2", GPIO_NUM_14);
static const InternalPinPtr IOC1 = InternalPin::registerPin("C1", GPIO_NUM_21);
static const InternalPinPtr IOD4 = InternalPin::registerPin("D4", GPIO_NUM_47);
static const InternalPinPtr IOD3 = InternalPin::registerPin("D3", GPIO_NUM_48);

static const InternalPinPtr SDA = InternalPin::registerPin("SDA", GPIO_NUM_35);
static const InternalPinPtr SCL = InternalPin::registerPin("SCL", GPIO_NUM_36);

static const InternalPinPtr IOD1 = InternalPin::registerPin("D1", GPIO_NUM_37);
static const InternalPinPtr IOD2 = InternalPin::registerPin("D2", GPIO_NUM_38);

static const InternalPinPtr TCK = InternalPin::registerPin("TCK", GPIO_NUM_39);
static const InternalPinPtr TDO = InternalPin::registerPin("TDO", GPIO_NUM_40);
static const InternalPinPtr TDI = InternalPin::registerPin("TDI", GPIO_NUM_41);
static const InternalPinPtr TMS = InternalPin::registerPin("TMS", GPIO_NUM_42);
static const InternalPinPtr RXD0 = InternalPin::registerPin("RXD0", GPIO_NUM_44);
static const InternalPinPtr TXD0 = InternalPin::registerPin("TXD0", GPIO_NUM_43);
}    // namespace pins

class UglyDucklingMk5 : public DeviceDefinition<Mk5Settings> {
public:
    explicit UglyDucklingMk5()
        : DeviceDefinition(pins::STATUS, pins::BOOT) {
    }

protected:
    void registerDeviceSpecificPeripheralFactories(const std::shared_ptr<PeripheralManager>& peripheralManager, const PeripheralServices& services, const std::shared_ptr<Mk5Settings>& /*settings*/) override {
        auto motorA = std::make_shared<Drv8874Driver>(
            services.pwmManager,
            pins::AIN1,
            pins::AIN2,
            pins::AIPROPI,
            pins::NFault,
            pins::NSLEEP);

        auto motorB = std::make_shared<Drv8874Driver>(
            services.pwmManager,
            pins::BIN1,
            pins::BIN2,
            pins::BIPROPI,
            pins::NFault,
            pins::NSLEEP);

        std::map<std::string, std::shared_ptr<PwmMotorDriver>> motors = { { "a", motorA }, { "b", motorB } };

        peripheralManager->registerFactory(valve::makeFactory(motors, ValveControlStrategyType::Latching));
        peripheralManager->registerFactory(flow_meter::makeFactory());
        peripheralManager->registerFactory(chicken_door::makeFactory(motors));
    }
};

}    // namespace farmhub::devices
