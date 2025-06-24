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

class EnvironmentSensor {
public:
    virtual ~EnvironmentSensor() = default;

    virtual double getTemperature() = 0;
    virtual double getHumidity() = 0;
};

template <std::derived_from<EnvironmentSensor> TComponent>
class I2CEnvironmentFactory
    : public PeripheralFactory<I2CDeviceConfig, EmptyConfiguration> {
public:
    I2CEnvironmentFactory(const std::string& sensorType, uint8_t defaultAddress)
        : PeripheralFactory<I2CDeviceConfig, EmptyConfiguration>("environment:" + sensorType, "environment")
        , sensorType(sensorType)
        , defaultAddress(defaultAddress) {
    }

    std::shared_ptr<Peripheral<EmptyConfiguration>> createPeripheral(const std::string& name, const std::shared_ptr<I2CDeviceConfig>& deviceConfig, const std::shared_ptr<MqttRoot>& /*mqttRoot*/, const PeripheralServices& services) override {
        auto i2cConfig = deviceConfig->parse(defaultAddress);
        LOGI("Creating %s sensor %s with %s",
            sensorType.c_str(), name.c_str(), i2cConfig.toString().c_str());
        auto sensor = std::make_shared<TComponent>(sensorType, services.i2c, i2cConfig);
        services.telemetryCollector->registerProvider("temperature", name, [sensor](JsonObject& telemetryJson) {
            telemetryJson["value"] = sensor->getTemperature();
        });
        services.telemetryCollector->registerProvider("humidity", name, [sensor](JsonObject& telemetryJson) {
            telemetryJson["value"] = sensor->getHumidity();
        });
        return std::make_shared<SimplePeripheral>(name, sensor);
    }

private:
    const std::string sensorType;
    const uint8_t defaultAddress;
};

}    // namespace farmhub::peripherals::environment
