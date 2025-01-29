#pragma once

#include <concepts>
#include <memory>

#include <kernel/Configuration.hpp>
#include <kernel/I2CManager.hpp>

#include <peripherals/I2CConfig.hpp>
#include <peripherals/Peripheral.hpp>

using namespace farmhub::kernel;
using namespace farmhub::kernel::mqtt;
using namespace farmhub::peripherals;
using std::make_unique;
using std::unique_ptr;

namespace farmhub::peripherals::environment {

template <std::derived_from<Component> TComponent>
class Environment
    : public Peripheral<EmptyConfiguration> {
public:
    Environment(
        const std::string& name,
        const std::string& sensorType,
        shared_ptr<MqttRoot> mqttRoot,
        std::shared_ptr<I2CManager> i2c,
        I2CConfig config)
        : Peripheral<EmptyConfiguration>(name, mqttRoot)
        , component(name, sensorType, mqttRoot, i2c, config) {
    }

    void populateTelemetry(JsonObject& telemetryJson) override {
        component.populateTelemetry(telemetryJson);
    }

private:
    TComponent component;
};

template <std::derived_from<Component> TComponent>
class I2CEnvironmentFactory
    : public PeripheralFactory<I2CDeviceConfig, EmptyConfiguration> {
public:
    I2CEnvironmentFactory(const std::string& sensorType, uint8_t defaultAddress)
        : PeripheralFactory<I2CDeviceConfig, EmptyConfiguration>("environment:" + sensorType, "environment")
        , sensorType(sensorType)
        , defaultAddress(defaultAddress) {
    }

    unique_ptr<Peripheral<EmptyConfiguration>> createPeripheral(const std::string& name, const std::shared_ptr<I2CDeviceConfig> deviceConfig, shared_ptr<MqttRoot> mqttRoot, const PeripheralServices& services) override {
        auto i2cConfig = deviceConfig->parse(defaultAddress);
        LOGI("Creating %s sensor %s with %s",
            sensorType.c_str(), name.c_str(), i2cConfig.toString().c_str());
        return make_unique<Environment<TComponent>>(name, sensorType, mqttRoot, services.i2c, i2cConfig);
    }

private:
    const std::string sensorType;
    const uint8_t defaultAddress;
};

}    // namespace farmhub::peripherals::environment
