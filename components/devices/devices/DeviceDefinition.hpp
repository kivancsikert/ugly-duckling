#pragma once

#include <concepts>
#include <list>
#include <memory>
#include <unordered_map>
#include <utility>

#include <ArduinoJson.h>

#include <Log.hpp>
#include <PcntManager.hpp>
#include <PulseCounter.hpp>
#include <PwmManager.hpp>
#include <devices/DeviceConfiguration.hpp>
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

using namespace farmhub::kernel;
using namespace farmhub::kernel::drivers;
using namespace farmhub::peripherals;

namespace farmhub::devices {

enum class Revision {
    Rev1,
    Rev2,
    Rev3,
};

constexpr std::string_view to_string(Revision rev) {
    switch (rev) {
        case Revision::Rev1:
            return "Rev1";
        case Revision::Rev2:
            return "Rev2";
        case Revision::Rev3:
            return "Rev3";
    }
    return "<Unknown>";
}

Revision from_string(std::string_view str) {
    static const std::unordered_map<std::string_view, Revision> map = {
        { "Rev1", Revision::Rev1 },
        { "Rev2", Revision::Rev2 },
        { "Rev3", Revision::Rev3 },
    };
    auto it = map.find(str);
    if (it == map.end()) {
        throw std::invalid_argument("Invalid Revision: " + std::string(str));
    }
    return it->second;
}

void convertToJson(const Revision& src, JsonVariant dst) {
    dst.set(to_string(src));
}

bool convertFromJson(const JsonVariant& src, Revision& dst) {
    if (!src.is<std::string>()) {
        return false;
    }
    try {
        dst = from_string(src.as<std::string>());
        return true;
    } catch (const std::invalid_argument&) {
        return false;
    }
}

class DeviceDefinition {
public:
    DeviceDefinition(
        const std::string& model,
        Revision revision,
        PinPtr statusPin,
        InternalPinPtr bootPin)
        : model(model)
        , revision(revision)
        , statusPin(std::move(statusPin))
        , bootPin(std::move(bootPin)) {
    }

    virtual ~DeviceDefinition() = default;

    void registerPeripheralFactories(std::shared_ptr<PeripheralManager> peripheralManager, PeripheralServices services) {
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

        registerDeviceSpecificPeripheralFactories(peripheralManager, services);
    }

    /**
     * @brief Returns zero or more JSON configurations for any built-in peripheral of the device.
     */
    virtual std::list<std::string> getBuiltInPeripherals() {
        return {};
    }

    const std::string model;
    const Revision revision;
    const PinPtr statusPin;
    const InternalPinPtr bootPin;

    const std::string getDescription() const {
        return model + " " + std::string(to_string(revision));
    }

    virtual std::shared_ptr<DeviceConfiguration> getDeviceConfiguration() const = 0;

protected:
    virtual void registerDeviceSpecificPeripheralFactories(const std::shared_ptr<PeripheralManager>& peripheralManager, const PeripheralServices& services) {
    }
};

class DeviceFactory {
public:
    explicit DeviceFactory(Revision revision)
        : revision(revision) {
    }

    virtual ~DeviceFactory() = default;

    virtual std::shared_ptr<BatteryDriver> createBatteryDriver(const std::shared_ptr<I2CManager>& /*i2c*/) = 0;

    virtual std::shared_ptr<DeviceDefinition> createDeviceDefinition(const std::shared_ptr<FileSystem>& /*fileSystem*/, const std::string& /*configPath*/) = 0;

    const Revision revision;
};

template <std::derived_from<DeviceConfiguration> TDeviceConfiguration>
class TypedDeviceDefinition : public DeviceDefinition {
public:
    TypedDeviceDefinition(
        const std::string& model,
        Revision revision,
        PinPtr statusPin,
        InternalPinPtr bootPin,
        const std::shared_ptr<TDeviceConfiguration>& deviceConfig)
        : DeviceDefinition(model, revision, std::move(statusPin), std::move(bootPin))
        , deviceConfig(deviceConfig) {
    }

    std::shared_ptr<DeviceConfiguration> getDeviceConfiguration() const override {
        return deviceConfig;
    }

private:
    std::shared_ptr<TDeviceConfiguration> deviceConfig;
};

}    // namespace farmhub::devices
