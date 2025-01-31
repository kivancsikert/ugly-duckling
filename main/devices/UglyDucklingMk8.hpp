#pragma once

#include <kernel/FileSystem.hpp>
#include <kernel/Pin.hpp>
#include <kernel/drivers/LedDriver.hpp>

#include <peripherals/Peripheral.hpp>

#include <devices/DeviceDefinition.hpp>

using namespace farmhub::kernel;

namespace farmhub::devices {

namespace pins {
static InternalPinPtr BOOT = InternalPin::registerPin("BOOT", GPIO_NUM_9);
static InternalPinPtr STATUS = InternalPin::registerPin("STATUS", GPIO_NUM_1);
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
    UglyDucklingMk8(std::shared_ptr<Mk8Config> config)
        : DeviceDefinition(pins::STATUS, pins::BOOT) {
    }

protected:
    void registerDeviceSpecificPeripheralFactories(std::shared_ptr<PeripheralManager> peripheralManager, PeripheralServices services, std::shared_ptr<Mk8Config> deviceConfig) override {
    }
};

}    // namespace farmhub::devices
