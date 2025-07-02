#pragma once

#include <chrono>
#include <memory>

#include <esp_system.h>

#include <bh1750.h>

#include <Configuration.hpp>
#include <I2CManager.hpp>
#include <Telemetry.hpp>

#include <peripherals/I2CSettings.hpp>
#include <peripherals/Peripheral.hpp>
#include <peripherals/light_sensor/LightSensor.hpp>
#include <utility>

using namespace std::chrono;
using namespace std::chrono_literals;

using namespace farmhub::kernel;
using namespace farmhub::peripherals;

namespace farmhub::peripherals::light_sensor {

class Bh1750Settings
    : public I2CSettings {
public:
    Property<seconds> measurementFrequency { this, "measurementFrequency", 1s };
    Property<seconds> latencyInterval { this, "latencyInterval", 5s };
};

class Bh1750 final
    : public LightSensor {
public:
    Bh1750(
        const std::string& name,
        const std::shared_ptr<I2CManager>& /*i2c*/,
        const I2CConfig& config,
        seconds measurementFrequency,
        seconds latencyInterval)
        : LightSensor(name, measurementFrequency, latencyInterval) {

        LOGI("Initializing BH1750 light sensor with %s",
            config.toString().c_str());

        // TODO Use I2CManager to create device
        ESP_ERROR_THROW(bh1750_init_desc(&sensor, config.address, I2C_NUM_0, config.sda->getGpio(), config.scl->getGpio()));
        ESP_ERROR_THROW(bh1750_setup(&sensor, BH1750_MODE_CONTINUOUS, BH1750_RES_LOW));

        runLoop();
    }

protected:
    double readLightLevel() override {
        uint16_t lightLevel;
        if (bh1750_read(&sensor, &lightLevel) != ESP_OK) {
            LOGE("Could not read light level");
        }
        return lightLevel;
    }

private:
    i2c_dev_t sensor {};
};

class Bh1750Factory
    : public PeripheralFactory<Bh1750Settings> {
public:
    Bh1750Factory()
        : PeripheralFactory<Bh1750Settings>("light-sensor:bh1750", "light-sensor") {
    }

    std::shared_ptr<Peripheral<EmptyConfiguration>> createPeripheral(PeripheralInitParameters& params, const std::shared_ptr<Bh1750Settings>& settings) override {
        I2CConfig i2cConfig = settings->parse(0x23);
        auto sensor = std::make_shared<Bh1750>(params.name, params.services.i2c, i2cConfig, settings->measurementFrequency.get(), settings->latencyInterval.get());
        params.registerFeature("light", [sensor](JsonObject& telemetryJson) {
            telemetryJson["value"] = sensor->getCurrentLevel();
        });
        return std::make_shared<SimplePeripheral>(params.name, sensor);
    }
};

}    // namespace farmhub::peripherals::light_sensor
