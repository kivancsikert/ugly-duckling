#pragma once

#include <concepts>
#include <list>
#include <memory>

#include <ArduinoJson.h>

#include <devices/DeviceConfiguration.hpp>

#include <kernel/Kernel.hpp>
#include <kernel/Log.hpp>
#include <kernel/PcntManager.hpp>
#include <kernel/PulseCounter.hpp>
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

class DeviceDefinition {
public:
    DeviceDefinition(PinPtr statusPin, InternalPinPtr bootPin)
        : statusPin(statusPin)
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

    virtual std::shared_ptr<BatteryDriver> createBatteryDriver(std::shared_ptr<I2CManager> i2c) {
        return nullptr;
    }

public:
    const PinPtr statusPin;
    const InternalPinPtr bootPin;

    const std::shared_ptr<PcntManager> pcnt { std::make_shared<PcntManager>() };
    const std::shared_ptr<PulseCounterManager> pulseCounterManager { std::make_shared<PulseCounterManager>() };
    const std::shared_ptr<PwmManager> pwm { std::make_shared<PwmManager>() };

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
