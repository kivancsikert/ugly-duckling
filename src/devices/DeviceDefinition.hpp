#pragma once

#include <list>

#include <SHT2x.h>
#include <SHT31.h>

#include <devices/Peripheral.hpp>
#include <kernel/Kernel.hpp>
#include <kernel/PwmManager.hpp>
#include <kernel/drivers/BatteryDriver.hpp>
#include <kernel/drivers/LedDriver.hpp>

#include <peripherals/environment/Environment.hpp>
#include <peripherals/environment/Sht2xComponent.hpp>
#include <peripherals/environment/Sht31Component.hpp>

#include <version.h>

using namespace farmhub::kernel;
using namespace farmhub::kernel::drivers;
using namespace farmhub::peripherals::environment;

namespace farmhub::devices {

class DeviceConfiguration : public ConfigurationSection {
public:
    DeviceConfiguration(const String& defaultModel)
        : model(this, "model", defaultModel)
        , instance(this, "instance", getMacAddress()) {
    }

    Property<String> model;
    Property<String> instance;
    Property<String> location { this, "location" };

    NamedConfigurationEntry<RtcDriver::Config> ntp { this, "ntp" };

    ArrayProperty<JsonAsString> peripherals { this, "peripherals" };

    Property<bool> sleepWhenIdle { this, "sleepWhenIdle", false };

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
        peripheralManager.registerFactory(sht3xFactory);
        peripheralManager.registerFactory(sht2xFactory);
        peripheralManager.registerFactory(htu2xFactory);
        registerDeviceSpecificPeripheralFactories(peripheralManager);
    }

    virtual void registerDeviceSpecificPeripheralFactories(PeripheralManager& peripheralManager) {
    }

    /**
     * @brief Returns zero or more JSON configurations for any built-in peripheral of the device.
     */
    virtual std::list<String> getBuiltInPeripherals() {
        return {};
    }

public:
    LedDriver statusLed;
    PwmManager pwm;

private:
    ConfigurationFile<TDeviceConfiguration> configFile { FileSystem::get(), "/device-config.json" };
    ConfigurationFile<MqttDriver::Config> mqttConfigFile { FileSystem::get(), "/mqtt-config.json" };

public:
    TDeviceConfiguration& config = configFile.config;
    MqttDriver::Config& mqttConfig = mqttConfigFile.config;

private:
    I2CEnvironmentFactory<Sht31Component> sht3xFactory { "sht3x", 0x44 /* Also supports 0x45 */ };
    I2CEnvironmentFactory<Sht2xComponent<SHT2x>> sht2xFactory { "sht2x", 0x40 /* Not configurable */ };
    I2CEnvironmentFactory<Sht2xComponent<HTU21>> htu2xFactory { "htu2x", 0x40 /* Not configurable */ };
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
