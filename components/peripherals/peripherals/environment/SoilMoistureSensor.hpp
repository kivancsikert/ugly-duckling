#pragma once

#include <memory>
#include <utility>

#include <peripherals/Peripheral.hpp>
#include <peripherals/api/ISoilMoistureSensor.hpp>

#include <utils/DebouncedMeasurement.hpp>

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

    // Exponential moving average alpha (0..1); 1 = no smoothing, 0 = no updates
    Property<double> alpha { this, "alpha", 1.0 };
};

class SoilMoistureSensor final
    : public api::ISoilMoistureSensor,
      public Peripheral {
public:
    SoilMoistureSensor(
        const std::string& name,
        int airValue,
        int waterValue,
        double alpha,
        const InternalPinPtr& pin)
        : Peripheral(name)
        , airValue(airValue)
        , waterValue(waterValue)
        , alpha(alpha)
        , pin(pin) {

        LOGI("Initializing soil moisture sensor '%s' on pin %s; air value: %d; water value: %d; EWMA alpha: %.2f",
            name.c_str(), pin->getName().c_str(), airValue, waterValue, alpha);
    }

    Percent getMoisture() override {
        return measurement.getValue();
    }

private:
    const int airValue;
    const int waterValue;
    const double alpha;
    AnalogPin pin;

    utils::DebouncedMeasurement<Percent> measurement {
        [this](const utils::DebouncedParams<Percent> params) -> std::optional<Percent> {
            std::optional<uint16_t> soilMoistureValue = pin.tryAnalogRead();
            if (!soilMoistureValue.has_value()) {
                LOGW("Failed to read soil moisture value from pin %s",
                    pin.getName().c_str());
                return std::nullopt;
            }
            LOGV("Soil moisture value: %d",
                soilMoistureValue.value());

            const double run = waterValue - airValue;
            const double rise = 100;
            const double delta = soilMoistureValue.value() - airValue;
            double currentValue = (delta * rise) / run;

            if (std::isnan(params.lastValue)) {
                return currentValue;
            }
            return (alpha * currentValue) + ((1 - alpha) * params.lastValue);
        },
        1s,
        NAN
    };
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
                settings->alpha.get(),
                settings->pin.get());
            params.registerFeature("moisture", [sensor](JsonObject& telemetryJson) {
                telemetryJson["value"] = sensor->getMoisture();
            });
            return sensor;
        });
}

}    // namespace farmhub::peripherals::environment
