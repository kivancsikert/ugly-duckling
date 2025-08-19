#pragma once

#include <concepts>
#include <list>
#include <memory>
#include <utility>

#include <ArduinoJson.h>

#include <devices/DeviceSettings.hpp>

#include <Log.hpp>
#include <PcntManager.hpp>
#include <PulseCounter.hpp>
#include <PwmManager.hpp>
#include <drivers/BatteryDriver.hpp>
#include <drivers/LedDriver.hpp>

#include <functions/PlotController.hpp>
#include <peripherals/Peripheral.hpp>
#include <peripherals/analog_meter/AnalogMeter.hpp>
#include <peripherals/environment/Ds18B20SoilSensor.hpp>
#include <peripherals/environment/Environment.hpp>
#include <peripherals/environment/Sht2xSensor.hpp>
#include <peripherals/environment/Sht3xSensor.hpp>
#include <peripherals/environment/SoilMoistureSensor.hpp>
#include <peripherals/fence/ElectricFenceMonitor.hpp>
#include <peripherals/light_sensor/Bh1750.hpp>
#include <peripherals/light_sensor/Tsl2591.hpp>
#include <peripherals/multiplexer/Xl9535.hpp>

using namespace farmhub::functions;
using namespace farmhub::kernel;
using namespace farmhub::kernel::drivers;
using namespace farmhub::peripherals;

namespace farmhub::devices {

#define UD_DEFINE_PIN3(GPIO, VAR, STR) \
    static const InternalPinPtr VAR = InternalPin::registerPin(STR, GPIO);

#define UD_DEFINE_PIN2(GPIO, VAR) \
    UD_DEFINE_PIN3(GPIO, VAR, #VAR)

#define UD_GET_MACRO(_1, _2, _3, NAME, ...) NAME

#define DEFINE_PIN(...) UD_GET_MACRO(__VA_ARGS__, UD_DEFINE_PIN3, UD_DEFINE_PIN2)(__VA_ARGS__)

template <std::derived_from<DeviceSettings> TDeviceSettings>
class DeviceDefinition {
public:
    DeviceDefinition(PinPtr statusPin, InternalPinPtr bootPin)
        : statusPin(std::move(statusPin))
        , bootPin(std::move(bootPin)) {
    }

    virtual ~DeviceDefinition() = default;

    void registerPeripheralFactories(const std::shared_ptr<PeripheralManager>& peripheralManager, const PeripheralServices& services, const std::shared_ptr<TDeviceSettings>& settings) {
        peripheralManager->registerFactory(environment::makeFactoryForSht3x());
        // TODO Unify these two factories
        peripheralManager->registerFactory(environment::makeFactoryForSht2x("sht2x"));
        peripheralManager->registerFactory(environment::makeFactoryForSht2x("htu2x"));

        peripheralManager->registerFactory(environment::makeFactoryForSoilMoisture());
        peripheralManager->registerFactory(environment::makeFactoryForDs18b20());

        peripheralManager->registerFactory(fence::makeFactory());

        peripheralManager->registerFactory(light_sensor::makeFactoryForBh1750());
        peripheralManager->registerFactory(light_sensor::makeFactoryForTsl2591());

        peripheralManager->registerFactory(multiplexer::makeFactoryForXl9535());

        peripheralManager->registerFactory(analog_meter::makeFactory());

        registerDeviceSpecificPeripheralFactories(peripheralManager, services, settings);
    }

    void registerFunctionFactories(const std::shared_ptr<FunctionManager>& functionManager) {
        functionManager->registerFactory(plot_controller::makeFactory());
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
    virtual void registerDeviceSpecificPeripheralFactories(const std::shared_ptr<PeripheralManager>& peripheralManager, const PeripheralServices& services, const std::shared_ptr<TDeviceSettings>& settings) {
    }
};

}    // namespace farmhub::devices
