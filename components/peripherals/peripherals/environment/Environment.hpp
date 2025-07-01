#pragma once

#include <concepts>
#include <memory>

#include <Configuration.hpp>
#include <I2CManager.hpp>

#include <peripherals/I2CSettings.hpp>
#include <peripherals/Peripheral.hpp>
#include <utility>

using namespace farmhub::kernel;
using namespace farmhub::kernel::mqtt;
using namespace farmhub::peripherals;

namespace farmhub::peripherals::environment {

class EnvironmentSensor {
public:
    virtual ~EnvironmentSensor() = default;

    virtual double getTemperature() = 0;
    virtual double getMoisture() = 0;
};

template <std::derived_from<EnvironmentSensor> TSensor>
class I2CEnvironmentFactory
    : public PeripheralFactory<I2CSettings> {
public:
    I2CEnvironmentFactory(const std::string& sensorType, uint8_t defaultAddress)
        : PeripheralFactory<I2CSettings>("environment:" + sensorType, "environment")
        , sensorType(sensorType)
        , defaultAddress(defaultAddress) {
    }

    std::shared_ptr<Peripheral<EmptyConfiguration>> createPeripheral(const std::string& name, const std::shared_ptr<I2CSettings>& settings, const std::shared_ptr<MqttRoot>& /*mqttRoot*/, const PeripheralServices& services) override {
        auto i2cConfig = settings->parse(defaultAddress);
        LOGI("Creating %s sensor %s with %s",
            sensorType.c_str(), name.c_str(), i2cConfig.toString().c_str());
        auto sensor = std::make_shared<TSensor>(sensorType, services.i2c, i2cConfig);
        services.telemetryCollector->registerFeature("temperature", name, [sensor](JsonObject& telemetryJson) {
            telemetryJson["value"] = sensor->getTemperature();
        });
        services.telemetryCollector->registerFeature("moisture", name, [sensor](JsonObject& telemetryJson) {
            telemetryJson["value"] = sensor->getMoisture();
        });
        return std::make_shared<SimplePeripheral>(name, sensor);
    }

private:
    const std::string sensorType;
    const uint8_t defaultAddress;
};

}    // namespace farmhub::peripherals::environment
