#pragma once

#include <memory>
#include <utility>

#include <peripherals/Peripheral.hpp>
#include <peripherals/api/ISoilMoistureSensor.hpp>

using namespace farmhub::kernel;
using namespace farmhub::peripherals;

namespace farmhub::peripherals::environment {

class SoilMoistureSensorSettings
    : public ConfigurationSection {
public:
    Property<InternalPinPtr> pin { this, "pin" };
    // These values need calibrating for each sensor
    Property<uint16_t> air { this, "air", 3000 };
    Property<uint16_t> water { this, "water", 1000 };
};

class SoilMoistureSensor final
    : public api::ISoilMoistureSensor,
      public Peripheral {
public:
    SoilMoistureSensor(
        const std::string& name,
        int airValue,
        int waterValue,
        const InternalPinPtr& pin)
        : Peripheral(name)
        , airValue(airValue)
        , waterValue(waterValue)
        , pin(pin) {

        LOGI("Initializing soil moisture sensor '%s' on pin %s; air value: %d; water value: %d",
            name.c_str(), pin->getName().c_str(), airValue, waterValue);
    }

    double getMoisture() override {
        std::optional<uint16_t> soilMoistureValue = pin.analogRead();
        if (!soilMoistureValue.has_value()) {
            LOGD("Failed to read soil moisture value");
            return std::numeric_limits<double>::quiet_NaN();
        }
        LOGV("Soil moisture value: %d",
            soilMoistureValue.value());

        const double run = waterValue - airValue;
        const double rise = 100;
        const double delta = soilMoistureValue.value() - airValue;
        double moisture = (delta * rise) / run;

        return moisture;
    }

private:
    const int airValue;
    const int waterValue;
    AnalogPin pin;
};

inline PeripheralFactory makeFactoryForSoilMoisture() {
    return makePeripheralFactory<ISoilMoistureSensor, SoilMoistureSensor, SoilMoistureSensorSettings>(
        "environment:soil-moisture",
        "environment",
        [](PeripheralInitParameters& params, const std::shared_ptr<SoilMoistureSensorSettings>& settings) {
            auto sensor = std::make_shared<SoilMoistureSensor>(
                params.name,
                settings->air.get(),
                settings->water.get(),
                settings->pin.get());
            params.registerFeature("moisture", [sensor](JsonObject& telemetryJson) {
                telemetryJson["value"] = sensor->getMoisture();
            });
            return sensor;
        });
}

}    // namespace farmhub::peripherals::environment
