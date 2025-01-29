#pragma once

#include <kernel/FileSystem.hpp>
#include <kernel/Kernel.hpp>
#include <kernel/Pin.hpp>
#include <kernel/Service.hpp>
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

class UglyDucklingMk8 : public DeviceDefinition {
public:
    UglyDucklingMk8(std::shared_ptr<Mk8Config> config)
        : DeviceDefinition(pins::STATUS, pins::BOOT) {
    }

    void registerDeviceSpecificPeripheralFactories(PeripheralManager& peripheralManager) override {
    }
};

}    // namespace farmhub::devices
