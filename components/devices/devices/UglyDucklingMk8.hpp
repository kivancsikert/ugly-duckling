#pragma once

#include <FileSystem.hpp>
#include <Pin.hpp>
#include <drivers/LedDriver.hpp>

#include <peripherals/Peripheral.hpp>

#include <devices/DeviceDefinition.hpp>

using namespace farmhub::kernel;

namespace farmhub::devices {

namespace pins {
static const InternalPinPtr BOOT = InternalPin::registerPin("BOOT", GPIO_NUM_9);
static const InternalPinPtr STATUS = InternalPin::registerPin("STATUS", GPIO_NUM_1);
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
    explicit UglyDucklingMk8(const std::shared_ptr<Mk8Config>& config)
        : DeviceDefinition(pins::STATUS, pins::BOOT) {
    }

protected:
    void registerDeviceSpecificPeripheralFactories(const std::shared_ptr<PeripheralManager>& peripheralManager, const PeripheralServices& services, const std::shared_ptr<Mk8Config>& deviceConfig) override {
    }
};

}    // namespace farmhub::devices
