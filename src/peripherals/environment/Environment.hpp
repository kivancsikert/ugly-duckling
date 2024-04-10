#pragma once

#include <memory>

#include <kernel/Configuration.hpp>
#include <kernel/I2CManager.hpp>
#include <kernel/Log.hpp>
#include <kernel/drivers/MqttDriver.hpp>

#include <peripherals/I2CConfig.hpp>
#include <peripherals/Peripheral.hpp>

using namespace farmhub::kernel;
using namespace farmhub::kernel::drivers;
using namespace farmhub::peripherals;
using std::make_unique;
using std::unique_ptr;

namespace farmhub::peripherals::environment {

template <typename TComponent>
class Environment
    : public Peripheral<EmptyConfiguration> {
public:
    Environment(
        const String& name,
        const String& sensorType,
        shared_ptr<MqttDriver::MqttRoot> mqttRoot,
        I2CManager& i2c,
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

template <typename TComponent>
class I2CEnvironmentFactory
    : public PeripheralFactory<I2CDeviceConfig, EmptyConfiguration> {
public:
    I2CEnvironmentFactory(const String& sensorType, uint8_t defaultAddress)
        : PeripheralFactory<I2CDeviceConfig, EmptyConfiguration>("environment:" + sensorType, "environment")
        , sensorType(sensorType)
        , defaultAddress(defaultAddress) {
    }

    unique_ptr<Peripheral<EmptyConfiguration>> createPeripheral(const String& name, const I2CDeviceConfig& deviceConfig, shared_ptr<MqttDriver::MqttRoot> mqttRoot, PeripheralServices& services) override {
        auto i2cConfig = deviceConfig.parse(defaultAddress, GPIO_NUM_NC, GPIO_NUM_NC);
        Log.info("Creating %s sensor %s with %s",
            sensorType.c_str(), name.c_str(), i2cConfig.toString().c_str());
        return make_unique<Environment<TComponent>>(name, sensorType, mqttRoot, services.i2c, i2cConfig);
    }

private:
    const String sensorType;
    const uint8_t defaultAddress;
};

}    // namespace farmhub::peripherals::environment
