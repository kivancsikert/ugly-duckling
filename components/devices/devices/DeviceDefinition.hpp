#pragma once

#include <concepts>
#include <list>
#include <memory>

#include <ArduinoJson.h>

#include <devices/DeviceConfiguration.hpp>

#include <Log.hpp>
#include <PcntManager.hpp>
#include <PulseCounter.hpp>
#include <PwmManager.hpp>
#include <drivers/BatteryDriver.hpp>
#include <drivers/LedDriver.hpp>

#include <peripherals/Peripheral.hpp>
#include <peripherals/analog_meter/AnalogMeter.hpp>
#include <peripherals/environment/Ds18B20SoilSensor.hpp>
#include <peripherals/environment/Environment.hpp>
#include <peripherals/environment/Sht2xComponent.hpp>
#include <peripherals/environment/Sht3xComponent.hpp>
#include <peripherals/environment/SoilMoistureSensor.hpp>
#include <peripherals/fence/ElectricFenceMonitor.hpp>
#include <peripherals/light_sensor/Bh1750.hpp>
#include <peripherals/light_sensor/Tsl2591.hpp>
#include <peripherals/multiplexer/Xl9535.hpp>
#include <utility>

using namespace farmhub::kernel;
using namespace farmhub::kernel::drivers;
using namespace farmhub::peripherals;

namespace farmhub::devices {

template <std::derived_from<DeviceConfiguration> TDeviceConfiguration>
class DeviceDefinition {
public:
    DeviceDefinition(PinPtr statusPin, InternalPinPtr bootPin)
        : statusPin(std::move(statusPin))
        , bootPin(std::move(bootPin)) {
    }

    virtual ~DeviceDefinition() = default;

    virtual void registerPeripheralFactories(std::shared_ptr<PeripheralManager> peripheralManager, PeripheralServices services, std::shared_ptr<TDeviceConfiguration> deviceConfig) {
        peripheralManager->registerFactory(std::make_unique<environment::I2CEnvironmentFactory<environment::Sht3xComponent>>("sht3x", 0x44 /* Also supports 0x45 */));
        // TODO Unify these two factories
        peripheralManager->registerFactory(std::make_unique<environment::I2CEnvironmentFactory<environment::Sht2xComponent>>("sht2x", 0x40 /* Not configurable */));
        peripheralManager->registerFactory(std::make_unique<environment::I2CEnvironmentFactory<environment::Sht2xComponent>>("htu2x", 0x40 /* Not configurable */));

        peripheralManager->registerFactory(std::make_unique<environment::SoilMoistureSensorFactory>());
        peripheralManager->registerFactory(std::make_unique<environment::Ds18B20SoilSensorFactory>());

        peripheralManager->registerFactory(std::make_unique<fence::ElectricFenceMonitorFactory>());

        peripheralManager->registerFactory(std::make_unique<light_sensor::Bh1750Factory>());
        peripheralManager->registerFactory(std::make_unique<light_sensor::Tsl2591Factory>());

        peripheralManager->registerFactory(std::make_unique<multiplexer::Xl9535Factory>());

        peripheralManager->registerFactory(std::make_unique<analog_meter::AnalogMeterFactory>());

        registerDeviceSpecificPeripheralFactories(peripheralManager, services, deviceConfig);
    }

    /**
     * @brief Returns zero or more JSON configurations for any built-in peripheral of the device.
     */
    virtual std::list<std::string> getBuiltInPeripherals() {
        return {};
    }

    static std::shared_ptr<BatteryDriver> createBatteryDriver(const std::shared_ptr<I2CManager>& /*i2c*/) {
        return nullptr;
    }

    const PinPtr statusPin;
    const InternalPinPtr bootPin;

protected:
    virtual void registerDeviceSpecificPeripheralFactories(const std::shared_ptr<PeripheralManager>& peripheralManager, const PeripheralServices& services, const std::shared_ptr<TDeviceConfiguration>& deviceConfig) {
    }
};

}    // namespace farmhub::devices
