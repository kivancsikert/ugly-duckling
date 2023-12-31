#pragma once

#include <devices/Peripheral.hpp>
#include <kernel/Kernel.hpp>
#include <kernel/PwmManager.hpp>
#include <kernel/drivers/BatteryDriver.hpp>
#include <kernel/drivers/LedDriver.hpp>

#include <version.h>

using namespace farmhub::kernel;
using namespace farmhub::kernel::drivers;

namespace farmhub::devices {

class DeviceConfiguration : public ConfigurationSection {
public:
    DeviceConfiguration(const String& defaultModel)
        : model(this, "model", defaultModel)
        , instance(this, "instance", getMacAddress()) {
    }

    Property<String> model;
    Property<String> instance;

    NamedConfigurationEntry<MqttDriver::Config> mqtt { this, "mqtt" };
    NamedConfigurationEntry<RtcDriver::Config> ntp { this, "ntp" };

    ArrayProperty<JsonAsString> peripherals { this, "peripherals" };

    virtual bool isResetButtonPressed() {
        return false;
    }

    virtual const String getHostname() {
        String hostname = instance.get();
        hostname.replace(":", "-");
        hostname.replace("?", "");
        return hostname;
    }
};

template <typename TDeviceConfiguration>
class DeviceDefinition {
public:
    DeviceDefinition(gpio_num_t statusPin)
        : statusLed("status", statusPin) {
    }

    virtual void registerPeripheralFactories(PeripheralManager& peripheralManager) {
    }

public:
    LedDriver statusLed;
    PwmManager pwm;

private:
    ConfigurationFile<TDeviceConfiguration> configFile { FileSystem::get(), "/device-config.json" };

public:
    TDeviceConfiguration& config = configFile.config;
};

template <typename TDeviceConfiguration>
class BatteryPoweredDeviceDefinition : public DeviceDefinition<TDeviceConfiguration> {
public:
    BatteryPoweredDeviceDefinition(gpio_num_t statusPin, gpio_num_t batteryPin, float batteryVoltageDividerRatio)
        : DeviceDefinition<TDeviceConfiguration>(statusPin)
        , batteryDriver(batteryPin, batteryVoltageDividerRatio) {
    }

public:
    BatteryDriver batteryDriver;
};

}    // namespace farmhub::devices
