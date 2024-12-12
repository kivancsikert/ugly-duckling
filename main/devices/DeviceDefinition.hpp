#pragma once

#include <list>
#include <memory>

#include <ArduinoJson.h>

#include <kernel/Kernel.hpp>
#include <kernel/Log.hpp>
#include <kernel/PcntManager.hpp>
#include <kernel/PwmManager.hpp>
#include <kernel/drivers/BatteryDriver.hpp>
#include <kernel/drivers/LedDriver.hpp>

#include <peripherals/Peripheral.hpp>
#include <peripherals/environment/Ds18B20SoilSensor.hpp>
#include <peripherals/environment/Environment.hpp>
#include <peripherals/environment/Sht2xComponent.hpp>
#include <peripherals/environment/Sht3xComponent.hpp>
#include <peripherals/environment/SoilMoistureSensor.hpp>
#include <peripherals/fence/ElectricFenceMonitor.hpp>
#include <peripherals/light_sensor/Bh1750.hpp>
#include <peripherals/light_sensor/Tsl2591.hpp>
#include <peripherals/multiplexer/Xl9535.hpp>

using namespace farmhub::kernel;
using namespace farmhub::kernel::drivers;
using namespace farmhub::peripherals::environment;

namespace farmhub::devices {

class DeviceConfiguration : public ConfigurationSection {
public:
    DeviceConfiguration(const std::string& defaultModel)
        : model(this, "model", defaultModel)
        , instance(this, "instance", getMacAddress()) {
    }

    Property<std::string> model;
    Property<std::string> id { this, "id", "UNIDENTIFIED" };
    Property<std::string> instance;
    Property<std::string> location { this, "location" };

    NamedConfigurationEntry<RtcDriver::Config> ntp { this, "ntp" };

    ArrayProperty<JsonAsString> peripherals { this, "peripherals" };

    Property<bool> sleepWhenIdle { this, "sleepWhenIdle", false };

    Property<Level> publishLogs { this, "publishLogs", Level::Info };

    virtual const std::string getHostname() {
        std::string hostname = instance.get();
        std::replace(hostname.begin(), hostname.end(), ':', '-');
        std::erase(hostname, '?');
        return hostname;
    }
};

template <typename TDeviceConfiguration>
class DeviceDefinition {
public:
    DeviceDefinition(PinPtr statusPin, InternalPinPtr bootPin)
        : statusLed("status", statusPin)
        , bootPin(bootPin) {
    }

    virtual void registerPeripheralFactories(PeripheralManager& peripheralManager) {
        peripheralManager.registerFactory(sht3xFactory);
        peripheralManager.registerFactory(sht2xFactory);
        peripheralManager.registerFactory(htu2xFactory);
        peripheralManager.registerFactory(ds18b20SoilSensorFactory);
        peripheralManager.registerFactory(soilMoistureSensorFactory);
        peripheralManager.registerFactory(electricFenceMonitorFactory);
        peripheralManager.registerFactory(bh1750Factory);
        peripheralManager.registerFactory(tsl2591Factory);
        peripheralManager.registerFactory(xl9535Factory);
        registerDeviceSpecificPeripheralFactories(peripheralManager);
    }

    virtual void registerDeviceSpecificPeripheralFactories(PeripheralManager& peripheralManager) {
    }

    /**
     * @brief Returns zero or more JSON configurations for any built-in peripheral of the device.
     */
    virtual std::list<std::string> getBuiltInPeripherals() {
        return {};
    }

    virtual std::shared_ptr<BatteryDriver> createBatteryDriver(I2CManager& i2c) {
        return nullptr;
    }

public:
    LedDriver statusLed;
    PcntManager pcnt;
    PwmManager pwm;
    const InternalPinPtr bootPin;

private:
    ConfigurationFile<TDeviceConfiguration> configFile { FileSystem::get(), "/device-config.json" };
    ConfigurationFile<MqttDriver::Config> mqttConfigFile { FileSystem::get(), "/mqtt-config.json" };

public:
    TDeviceConfiguration& config = configFile.config;
    MqttDriver::Config& mqttConfig = mqttConfigFile.config;

private:
    I2CEnvironmentFactory<Sht3xComponent> sht3xFactory { "sht3x", 0x44 /* Also supports 0x45 */ };
    // TODO Unify these two factories
    I2CEnvironmentFactory<Sht2xComponent> sht2xFactory { "sht2x", 0x40 /* Not configurable */ };
    I2CEnvironmentFactory<Sht2xComponent> htu2xFactory { "htu2x", 0x40 /* Not configurable */ };
    SoilMoistureSensorFactory soilMoistureSensorFactory;

    Ds18B20SoilSensorFactory ds18b20SoilSensorFactory;

    farmhub::peripherals::fence::ElectricFenceMonitorFactory electricFenceMonitorFactory;

    farmhub::peripherals::light_sensor::Bh1750Factory bh1750Factory;
    farmhub::peripherals::light_sensor::Tsl2591Factory tsl2591Factory;

    farmhub::peripherals::multiplexer::Xl9535Factory xl9535Factory;
};

}    // namespace farmhub::devices
