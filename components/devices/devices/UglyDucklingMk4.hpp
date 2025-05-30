#pragma once

#include <FileSystem.hpp>
#include <Pin.hpp>
#include <drivers/BatteryDriver.hpp>
#include <drivers/Drv8801Driver.hpp>
#include <drivers/LedDriver.hpp>

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

class Mk4Config
    : public DeviceConfiguration {
public:
    Mk4Config()
        : DeviceConfiguration("mk4") {
    }
};

namespace pins {
static const InternalPinPtr BOOT = InternalPin::registerPin("BOOT", GPIO_NUM_0);
static const InternalPinPtr STATUS = InternalPin::registerPin("STATUS", GPIO_NUM_26);

static const InternalPinPtr SOIL_MOISTURE = InternalPin::registerPin("SOIL_MOISTURE", GPIO_NUM_6);
static const InternalPinPtr SOIL_TEMP = InternalPin::registerPin("SOIL_TEMP", GPIO_NUM_7);

static const InternalPinPtr VALVE_EN = InternalPin::registerPin("VALVE_EN", GPIO_NUM_10);
static const InternalPinPtr VALVE_PH = InternalPin::registerPin("VALVE_PH", GPIO_NUM_11);
static const InternalPinPtr VALVE_FAULT = InternalPin::registerPin("VALVE_FAULT", GPIO_NUM_12);
static const InternalPinPtr VALVE_SLEEP = InternalPin::registerPin("VALVE_SLEEP", GPIO_NUM_13);
static const InternalPinPtr VALVE_MODE1 = InternalPin::registerPin("VALVE_MODE1", GPIO_NUM_14);
static const InternalPinPtr VALVE_MODE2 = InternalPin::registerPin("VALVE_MODE2", GPIO_NUM_15);
static const InternalPinPtr VALVE_CURRENT = InternalPin::registerPin("VALVE_CURRENT", GPIO_NUM_16);
static const InternalPinPtr FLOW = InternalPin::registerPin("FLOW", GPIO_NUM_17);

static const InternalPinPtr SDA = InternalPin::registerPin("SDA", GPIO_NUM_8);
static const InternalPinPtr SCL = InternalPin::registerPin("SCL", GPIO_NUM_9);
static const InternalPinPtr RXD0 = InternalPin::registerPin("RXD0", GPIO_NUM_44);
static const InternalPinPtr TXD0 = InternalPin::registerPin("TXD0", GPIO_NUM_43);
}    // namespace pins

class UglyDucklingMk4 : public DeviceDefinition<Mk4Config> {
public:
    UglyDucklingMk4(const std::shared_ptr<Mk4Config>& config)
        : DeviceDefinition(pins::STATUS, pins::BOOT) {
    }

    std::list<std::string> getBuiltInPeripherals() override {
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

protected:
    void registerDeviceSpecificPeripheralFactories(const std::shared_ptr<PeripheralManager>& peripheralManager, const PeripheralServices& services, const std::shared_ptr<Mk4Config>& deviceConfig) override {
        auto motor = std::make_shared<Drv8801Driver>(
            services.pwmManager,
            pins::VALVE_EN,
            pins::VALVE_PH,
            pins::VALVE_MODE1,
            pins::VALVE_MODE2,
            pins::VALVE_CURRENT,
            pins::VALVE_FAULT,
            pins::VALVE_SLEEP);

        std::map<std::string, std::shared_ptr<PwmMotorDriver>> motors = { { "default", motor } };

        peripheralManager->registerFactory(std::make_unique<ValveFactory>(motors, ValveControlStrategyType::NormallyClosed));
        peripheralManager->registerFactory(std::make_unique<FlowMeterFactory>());
        peripheralManager->registerFactory(std::make_unique<FlowControlFactory>(motors, ValveControlStrategyType::NormallyClosed));
        peripheralManager->registerFactory(std::make_unique<ChickenDoorFactory>(motors));
    }
};

}    // namespace farmhub::devices
