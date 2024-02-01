#pragma once

#include <memory>

#include <devices/Peripheral.hpp>
#include <kernel/Configuration.hpp>
#include <kernel/drivers/MqttDriver.hpp>
#include <peripherals/I2CConfig.hpp>
#include <peripherals/environment/Sht3xComponent.hpp>

using namespace farmhub::devices;
using namespace farmhub::kernel;
using namespace farmhub::kernel::drivers;
using std::make_unique;
using std::unique_ptr;
namespace farmhub::peripherals::environment {

template <typename TComponent>
class Environment
    : public Peripheral<EmptyConfiguration> {
public:
    // TODO Use TComponentArgs&& instead
    Environment(const String& name, shared_ptr<MqttDriver::MqttRoot> mqttRoot, I2CConfig config)
        : Peripheral<EmptyConfiguration>(name, mqttRoot)
        , component(name, mqttRoot, config) {
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
    I2CEnvironmentFactory(const String& factoryType, uint8_t defaultAddress)
        : PeripheralFactory<I2CDeviceConfig, EmptyConfiguration>(factoryType, "environment")
        , defaultAddress(defaultAddress) {
    }

    unique_ptr<Peripheral<EmptyConfiguration>> createPeripheral(const String& name, const I2CDeviceConfig& deviceConfig, shared_ptr<MqttDriver::MqttRoot> mqttRoot) override {
        auto i2cConfig = deviceConfig.parse(defaultAddress, GPIO_NUM_NC, GPIO_NUM_NC);
        Log.infoln("Creating %s sensor %s with %s",
            factoryType.c_str(), name.c_str(), i2cConfig.toString().c_str());
        return make_unique<Environment<TComponent>>(name, mqttRoot, i2cConfig);
    }

private:
    const uint8_t defaultAddress;
};

}    // namespace farmhub::peripherals::environment
