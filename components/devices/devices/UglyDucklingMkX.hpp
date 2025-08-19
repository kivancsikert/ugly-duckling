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

class MkXSettings
    : public DeviceSettings {
public:
    MkXSettings()
        : DeviceSettings("mkx") {
    }
};

class UglyDucklingMkX : public DeviceDefinition<MkXSettings> {
public:
    explicit UglyDucklingMkX()
        : DeviceDefinition(pins::STATUS, pins::BOOT) {
    }

protected:
    void registerDeviceSpecificPeripheralFactories(const std::shared_ptr<PeripheralManager>& peripheralManager, const PeripheralServices& services, const std::shared_ptr<MkXSettings>& /*settings*/) override {
    }
};

}    // namespace farmhub::devices
