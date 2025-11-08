#pragma once

#include <chrono>
#include <memory>

#include <esp_system.h>

#include <Configuration.hpp>
#include <Pin.hpp>
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

class AnalogLightSensorSettings
    : public I2CSettings {
public:
    Property<InternalPinPtr> pin { this, "pin" };
    Property<seconds> measurementFrequency { this, "measurementFrequency", 1s };
    Property<seconds> latencyInterval { this, "latencyInterval", 5s };
};

class AnalogLightSensor final
    : public LightSensor {
public:
    AnalogLightSensor(
        const std::string& name,
        const InternalPinPtr& pin,
        seconds measurementFrequency,
        seconds latencyInterval)
        : LightSensor(name, measurementFrequency, latencyInterval)
        , pin(pin) {

        LOGI("Initializing analog light sensor on pin %s",
            pin->getName().c_str());

        runLoop();
    }

protected:
    double readLightLevel() override {
        // These constants should match the photo-resistor's "gamma" and "rl10" attributes
        const double GAMMA = 0.7;
        const double RL10 = 50;

        // Convert the analog value into lux value:
        auto analogValue = pin.analogRead();
        auto voltage = analogValue / 4096.0 * 5;
        auto resistance = 2000 * voltage / (1 - voltage / 5);
        auto lux = pow(RL10 * 1e3 * pow(10, GAMMA) / resistance, (1 / GAMMA));

        return lux;
    }

private:
    AnalogPin pin;
};

inline PeripheralFactory makeFactoryForAnalogLightSensor() {
    return makePeripheralFactory<ILightSensor, AnalogLightSensor, AnalogLightSensorSettings>(
        "light-sensor:analog",
        "light-sensor",
        [](PeripheralInitParameters& params, const std::shared_ptr<AnalogLightSensorSettings>& settings) {
            auto sensor = std::make_shared<AnalogLightSensor>(
                params.name,
                settings->pin.get(),
                settings->measurementFrequency.get(),
                settings->latencyInterval.get());
            params.registerFeature("light", [sensor](JsonObject& telemetryJson) {
                telemetryJson["value"] = sensor->getLightLevel();
            });
            return sensor;
        });
}

}    // namespace farmhub::peripherals::light_sensor
