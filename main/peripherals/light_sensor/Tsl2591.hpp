#pragma once

#include <chrono>
#include <memory>

#include <tsl2591.h>

#include <Component.hpp>
#include <Configuration.hpp>
#include <I2CManager.hpp>
#include <Telemetry.hpp>

#include <peripherals/I2CConfig.hpp>
#include <peripherals/Peripheral.hpp>
#include <peripherals/light_sensor/LightSensor.hpp>

using namespace std::chrono;
using namespace std::chrono_literals;

using namespace farmhub::kernel;
using namespace farmhub::peripherals;

namespace farmhub::peripherals::light_sensor {

static constexpr uint8_t TSL2591_ADDR = 0x29;

class Tsl2591DeviceConfig
    : public I2CDeviceConfig {
public:
    Property<seconds> measurementFrequency { this, "measurementFrequency", 1s };
    Property<seconds> latencyInterval { this, "latencyInterval", 5s };
};

class Tsl2591Component
    : public LightSensorComponent {
public:
    Tsl2591Component(
        const std::string& name,
        std::shared_ptr<MqttRoot> mqttRoot,
        std::shared_ptr<I2CManager> i2c,
        I2CConfig config,
        seconds measurementFrequency,
        seconds latencyInterval)
        : LightSensorComponent(name, mqttRoot, measurementFrequency, latencyInterval)
        , bus(i2c->getBusFor(config)) {

        LOGI("Initializing TSL2591 light sensor with %s",
            config.toString().c_str());

        ESP_ERROR_CHECK(tsl2591_init_desc(&sensor, bus->port, bus->sda->getGpio(), bus->scl->getGpio()));
        ESP_ERROR_CHECK(tsl2591_init(&sensor));

        // TODO Make these configurable
        ESP_ERROR_CHECK(tsl2591_set_power_status(&sensor, TSL2591_POWER_ON));
        ESP_ERROR_CHECK(tsl2591_set_als_status(&sensor, TSL2591_ALS_ON));
        ESP_ERROR_CHECK(tsl2591_set_gain(&sensor, TSL2591_GAIN_MEDIUM));
        ESP_ERROR_CHECK(tsl2591_set_integration_time(&sensor, TSL2591_INTEGRATION_300MS));

        runLoop();
    }

protected:
    double readLightLevel() override {
        esp_err_t res;
        float lux;
        if ((res = tsl2591_get_lux(&sensor, &lux)) != ESP_OK) {
            LOGD("Could not read light level: %s", esp_err_to_name(res));
            return std::numeric_limits<double>::quiet_NaN();
        } else {
            return lux;
        }
    }

private:
    std::shared_ptr<I2CBus> bus;
    tsl2591_t sensor {};
};

class Tsl2591
    : public Peripheral<EmptyConfiguration> {

public:
    Tsl2591(
        const std::string& name,
        std::shared_ptr<MqttRoot> mqttRoot,
        std::shared_ptr<I2CManager> i2c,
        const I2CConfig& config,
        seconds measurementFrequency,
        seconds latencyInterval)
        : Peripheral<EmptyConfiguration>(name, mqttRoot)
        , component(name, mqttRoot, i2c, config, measurementFrequency, latencyInterval) {
    }

    void populateTelemetry(JsonObject& telemetryJson) override {
        component.populateTelemetry(telemetryJson);
    }

private:
    Tsl2591Component component;
};

class Tsl2591Factory
    : public PeripheralFactory<Tsl2591DeviceConfig, EmptyConfiguration> {
public:
    Tsl2591Factory()
        : PeripheralFactory<Tsl2591DeviceConfig, EmptyConfiguration>("light-sensor:tsl2591", "light-sensor") {
    }

    std::unique_ptr<Peripheral<EmptyConfiguration>> createPeripheral(const std::string& name, const std::shared_ptr<Tsl2591DeviceConfig> deviceConfig, std::shared_ptr<MqttRoot> mqttRoot, const PeripheralServices& services) override {
        I2CConfig i2cConfig = deviceConfig->parse(TSL2591_ADDR);
        return std::make_unique<Tsl2591>(name, mqttRoot, services.i2c, i2cConfig, deviceConfig->measurementFrequency.get(), deviceConfig->latencyInterval.get());
    }
};

}    // namespace farmhub::peripherals::light_sensor
