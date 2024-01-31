#pragma once

#include <memory>

#include <devices/Peripheral.hpp>
#include <kernel/Configuration.hpp>
#include <kernel/drivers/MqttDriver.hpp>
#include <peripherals/I2CConfig.hpp>

#include "EnvironmentSht3xComponent.hpp"

using namespace farmhub::devices;
using namespace farmhub::kernel;
using namespace farmhub::kernel::drivers;
using std::make_unique;
using std::unique_ptr;
namespace farmhub::peripherals::environment {

class EnvironmentSht3x
    : public Peripheral<EmptyConfiguration> {
public:
    EnvironmentSht3x(const String& name, shared_ptr<MqttDriver::MqttRoot> mqttRoot, I2CConfig config)
        : Peripheral<EmptyConfiguration>(name, mqttRoot)
        , sht3x(name, mqttRoot, config) {
    }

    void populateTelemetry(JsonObject& telemetryJson) override {
        sht3x.populateTelemetry(telemetryJson);
    }

private:
    EnvironmentSht3xComponent sht3x;
};

class EnvironmentSht3xFactory
    : public PeripheralFactory<I2CDeviceConfig, EmptyConfiguration> {
public:
    EnvironmentSht3xFactory()
        : PeripheralFactory<I2CDeviceConfig, EmptyConfiguration>("environment:sht3x", "environment") {
    }

    unique_ptr<Peripheral<EmptyConfiguration>> createPeripheral(const String& name, const I2CDeviceConfig& deviceConfig, shared_ptr<MqttDriver::MqttRoot> mqttRoot) override {
        auto i2cConfig = deviceConfig.parse(SHT_DEFAULT_ADDRESS, GPIO_NUM_NC, GPIO_NUM_NC);
        Log.infoln("Creating SHT3x environment sensor %s with %s", name.c_str(), i2cConfig.toString().c_str());
        return make_unique<EnvironmentSht3x>(name, mqttRoot, i2cConfig);
    }
};

}    // namespace farmhub::peripherals::environment
