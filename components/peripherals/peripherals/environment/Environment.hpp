#pragma once

#include <concepts>
#include <memory>

#include <Configuration.hpp>
#include <I2CManager.hpp>

#include <peripherals/I2CConfig.hpp>
#include <peripherals/Peripheral.hpp>
#include <utility>

using namespace farmhub::kernel;
using namespace farmhub::kernel::mqtt;
using namespace farmhub::peripherals;

namespace farmhub::peripherals::environment {

class EnvironmentComponent {
public:
    virtual ~EnvironmentComponent() = default;

    virtual double getTemperature() = 0;
    virtual double getHumidity() = 0;
};

template <std::derived_from<EnvironmentComponent> TComponent>
class I2CEnvironmentFactory;

template <std::derived_from<EnvironmentComponent> TComponent>
class Environment
    : public Peripheral<EmptyConfiguration> {
public:
    Environment(
        const std::string& name,
        const std::string& sensorType,
        std::shared_ptr<I2CManager> i2c,
        I2CConfig config)
        : Peripheral<EmptyConfiguration>(name)
        , component(sensorType, i2c, config) {
    }

private:
    TComponent component;
    friend class I2CEnvironmentFactory<TComponent>;
};

template <std::derived_from<EnvironmentComponent> TComponent>
class I2CEnvironmentFactory
    : public PeripheralFactory<I2CDeviceConfig, EmptyConfiguration> {
public:
    I2CEnvironmentFactory(const std::string& sensorType, uint8_t defaultAddress)
        : PeripheralFactory<I2CDeviceConfig, EmptyConfiguration>("environment:" + sensorType, "environment")
        , sensorType(sensorType)
        , defaultAddress(defaultAddress) {
    }

    std::shared_ptr<Peripheral<EmptyConfiguration>> createPeripheral(const std::string& name, const std::shared_ptr<I2CDeviceConfig> deviceConfig, std::shared_ptr<MqttRoot>  /*mqttRoot*/, const PeripheralServices& services) override {
        auto i2cConfig = deviceConfig->parse(defaultAddress);
        LOGI("Creating %s sensor %s with %s",
            sensorType.c_str(), name.c_str(), i2cConfig.toString().c_str());
        auto peripheral = std::make_shared<Environment<TComponent>>(name, sensorType, services.i2c, i2cConfig);
        services.telemetryCollector->registerProvider("temperature", name, [peripheral](JsonObject& telemetryJson) {
            telemetryJson["value"] = peripheral->component.getTemperature();
        });
        services.telemetryCollector->registerProvider("humidity", name, [peripheral](JsonObject& telemetryJson) {
            telemetryJson["value"] = peripheral->component.getHumidity();
        });
        return peripheral;
    }

private:
    const std::string sensorType;
    const uint8_t defaultAddress;
};

}    // namespace farmhub::peripherals::environment
