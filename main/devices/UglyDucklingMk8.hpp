#pragma once

#include <kernel/FileSystem.hpp>
#include <kernel/Kernel.hpp>
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
#include <devices/Pin.hpp>

using namespace farmhub::kernel;
using namespace farmhub::peripherals::chicken_door;
using namespace farmhub::peripherals::flow_control;
using namespace farmhub::peripherals::flow_meter;
using namespace farmhub::peripherals::valve;

namespace farmhub::devices {

namespace pins {
static gpio_num_t BOOT = Pin::registerPin("BOOT", GPIO_NUM_0);
static gpio_num_t STATUS = Pin::registerPin("STATUS", GPIO_NUM_8);
}    // namespace pins

class Mk8Config
    : public DeviceConfiguration {
public:
    Mk8Config()
        : DeviceConfiguration("mk8") {
    }
};

class UglyDucklingMk8 : public DeviceDefinition<Mk8Config> {
public:
    UglyDucklingMk8()
        : DeviceDefinition<Mk8Config>(
              pins::STATUS,
              pins::BOOT) {
    }

    void registerDeviceSpecificPeripheralFactories(PeripheralManager& peripheralManager) override {
    }
};

}    // namespace farmhub::devices
