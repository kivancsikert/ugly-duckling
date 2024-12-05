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
static InternalPinPtr BOOT = InternalPin::registerPin("BOOT", GPIO_NUM_0);
static InternalPinPtr STATUS = InternalPin::registerPin("STATUS", GPIO_NUM_8);
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
