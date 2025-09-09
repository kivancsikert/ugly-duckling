#pragma once

#include <memory>

#include <Configuration.hpp>
#include <peripherals/Peripheral.hpp>
#include <peripherals/api/ISoilMoistureSensor.hpp>
#include <peripherals/api/ITemperatureSensor.hpp>

#include <utils/scheduling/MoistureKalmanFilter.hpp>

using namespace farmhub::utils::scheduling;

namespace farmhub::peripherals::environment {

/**
 * @brief Reports real soil moisture levels calculated from raw moisture and temperature data using a Kalman filter.
 */
class KalmanFilterSoilSensorSettings
    : public ConfigurationSection {
public:
    Property<std::string> rawMoistureSensor { this, "rawMoistureSensor" };
    Property<std::string> temperatureSensor { this, "temperatureSensor" };

    Property<Percent> initialMoisture { this, "initialMoisture" };
    Property<double> initialBeta { this, "initialBeta" };
    Property<Celsius> tempRef { this, "tempRef", 20.0 };
};

class KalmanFilterSoilSensor
    : public api::ISoilMoistureSensor,
      public Peripheral {
public:
    KalmanFilterSoilSensor(
        const std::string& name,
        const std::shared_ptr<api::ISoilMoistureSensor>& rawMoistureSensor,
        const std::shared_ptr<api::ITemperatureSensor>& tempSensor,
        Percent initialMoisture,
        double initialBeta,
        Celsius tempRef)
        : Peripheral(name)
        , kalmanFilter(initialMoisture, initialBeta, tempRef)
        , rawMoistureSensor(rawMoistureSensor)
        , tempSensor(tempSensor) {
        LOGI("Initializing Kalman filter soil moisture sensor '%s' wrapping moisture sensor '%s' "
             "and temperature sensor '%s', reference: %.1f C",
            name.c_str(), rawMoistureSensor->getName().c_str(), tempSensor->getName().c_str(), tempRef);
    }

    Percent getMoisture() override {
        auto rawMoisture = rawMoistureSensor->getMoisture();
        auto temp = tempSensor->getTemperature();
        kalmanFilter.update(rawMoisture, temp);
        return kalmanFilter.getMoistReal();
    }

private:
    MoistureKalmanFilter kalmanFilter;
    std::shared_ptr<api::ISoilMoistureSensor> rawMoistureSensor;
    std::shared_ptr<api::ITemperatureSensor> tempSensor;
};

inline PeripheralFactory makeFactoryForKalmanSoilMoisture() {
    return makePeripheralFactory<ISoilMoistureSensor, KalmanFilterSoilSensor, KalmanFilterSoilSensorSettings>(
        "environment:kalman-soil-moisture",
        "environment",
        [](PeripheralInitParameters& params, const std::shared_ptr<KalmanFilterSoilSensorSettings>& settings) {
            auto rawMoistureSensor = params.peripheral<api::ISoilMoistureSensor>(settings->rawMoistureSensor.get());
            auto tempSensor = params.peripheral<api::ITemperatureSensor>(settings->temperatureSensor.get());
            auto sensor = std::make_shared<KalmanFilterSoilSensor>(
                params.name,
                rawMoistureSensor,
                tempSensor,
                settings->initialMoisture.get(),
                settings->initialBeta.get(),
                settings->tempRef.get());
            params.registerFeature("moisture", [sensor](JsonObject& telemetryJson) {
                telemetryJson["value"] = sensor->getMoisture();
            });
            return sensor;
        });
}

}    // namespace farmhub::peripherals::environment
