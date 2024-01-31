#pragma once

#include <memory>

#include <devices/Peripheral.hpp>
#include <kernel/Configuration.hpp>
#include <kernel/drivers/MqttDriver.hpp>
#include <peripherals/I2CConfig.hpp>

#include "EnvironmentComponent.hpp"

using namespace farmhub::devices;
using namespace farmhub::kernel;
using namespace farmhub::kernel::drivers;
using std::make_unique;
using std::unique_ptr;
namespace farmhub::peripherals::environment {

class EnvironmentSht31
    : public Peripheral<EmptyConfiguration> {
public:
    EnvironmentSht31(const String& name, shared_ptr<MqttDriver::MqttRoot> mqttRoot, I2CConfig config)
        : Peripheral<EmptyConfiguration>(name, mqttRoot)
        , sht31(name, mqttRoot, config) {
    }

    void populateTelemetry(JsonObject& telemetryJson) override {
        sht31.populateTelemetry(telemetryJson);
    }

private:
    Sht31Component sht31;
};

class EnvironmentSht31Factory
    : public PeripheralFactory<I2CDeviceConfig, EmptyConfiguration> {
public:
    EnvironmentSht31Factory()
        : PeripheralFactory<I2CDeviceConfig, EmptyConfiguration>("environment:sht31", "environment") {
    }

    unique_ptr<Peripheral<EmptyConfiguration>> createPeripheral(const String& name, const I2CDeviceConfig& deviceConfig, shared_ptr<MqttDriver::MqttRoot> mqttRoot) override {
        auto i2cConfig = deviceConfig.parse(SHT_DEFAULT_ADDRESS, GPIO_NUM_NC, GPIO_NUM_NC);
        Log.infoln("Creating SHT31 environment sensor %s with %s", name.c_str(), i2cConfig.toString().c_str());
        return make_unique<EnvironmentSht31>(name, mqttRoot, i2cConfig);
    }
};

}    // namespace farmhub::peripherals::environment
