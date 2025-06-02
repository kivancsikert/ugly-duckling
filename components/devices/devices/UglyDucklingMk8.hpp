#pragma once

#include <FileSystem.hpp>
#include <Pin.hpp>
#include <drivers/LedDriver.hpp>

#include <peripherals/Peripheral.hpp>

#include <devices/DeviceDefinition.hpp>

using namespace farmhub::kernel;

namespace farmhub::devices::mk8 {

namespace pins {
static const InternalPinPtr BOOT = InternalPin::registerPin("BOOT", GPIO_NUM_9);
static const InternalPinPtr STATUS = InternalPin::registerPin("STATUS", GPIO_NUM_1);
}    // namespace pins

class Config
    : public DeviceConfiguration {
public:
    Config() {
    }
};

class Definition : public TypedDeviceDefinition<Config> {
public:
    explicit Definition(Revision revision, const std::shared_ptr<Config>& config)
        : TypedDeviceDefinition("mk8", revision, pins::STATUS, pins::BOOT, config) {
    }

protected:
    void registerDeviceSpecificPeripheralFactories(const std::shared_ptr<PeripheralManager>& peripheralManager, const PeripheralServices& services) override {
    }
};

class Factory : public DeviceFactory {
public:
    explicit Factory(Revision revision)
        : DeviceFactory(revision) {    // MK8 has no revisions
    }

    std::shared_ptr<BatteryDriver> createBatteryDriver(const std::shared_ptr<I2CManager>& /*i2c*/) override {
        return nullptr;    // No battery driver for MK8
    }

    std::shared_ptr<DeviceDefinition> createDeviceDefinition(const std::shared_ptr<FileSystem>& fileSystem, const std::string& configPath) override {
        auto config = loadConfiguration<Config>(fileSystem, configPath);
        return std::make_shared<Definition>(revision, config);
    }
};

}    // namespace farmhub::devices::mk8
