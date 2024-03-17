#pragma once

#include <chrono>
#include <deque>
#include <memory>

#include <Arduino.h>
#include <Wire.h>

#include <ArduinoLog.h>
#include <BH1750.h>

#include <kernel/Component.hpp>
#include <kernel/Configuration.hpp>
#include <kernel/I2CManager.hpp>
#include <kernel/Telemetry.hpp>
#include <peripherals/I2CConfig.hpp>
#include <peripherals/Peripheral.hpp>

using namespace std::chrono;
using namespace std::chrono_literals;

using namespace farmhub::kernel;
using namespace farmhub::peripherals;

namespace farmhub::peripherals::light_sensor {

class LightSensorComponent
    : public Component,
      public TelemetryProvider {
public:
    LightSensorComponent(
        const String& name,
        shared_ptr<MqttDriver::MqttRoot> mqttRoot,
        seconds measurementFrequency,
        seconds latencyInterval)
        : Component(name, mqttRoot)
        , measurementFrequency(measurementFrequency)
        , latencyInterval(latencyInterval) {
    }

    double getCurrentLevel() {
        Lock lock(updateAverageMutex);
        return averageLevel;
    }

    seconds getMeasurementFrequency() {
        return measurementFrequency;
    }

    void populateTelemetry(JsonObject& json) override {
        Lock lock(updateAverageMutex);
        json["light"] = averageLevel;
    }

protected:
    virtual double readLightLevel() = 0;

    void runLoop() {

        Task::loop(name, 3072, [this](Task& task) {
            auto currentLevel = readLightLevel();

            size_t maxMaxmeasurements = latencyInterval.count() / measurementFrequency.count();
            while (measurements.size() >= maxMaxmeasurements) {
                sum -= measurements.front();
                measurements.pop_front();
            }
            measurements.emplace_back(currentLevel);
            sum += currentLevel;

            {
                Lock lock(updateAverageMutex);
                averageLevel = sum / measurements.size();
            }

            task.delayUntil(measurementFrequency);
        });
    }

private:
    const seconds measurementFrequency;
    const seconds latencyInterval;

    std::deque<double> measurements;
    double sum;

    Mutex updateAverageMutex;
    double averageLevel = 0;
};

}    // namespace farmhub::peripherals::light_sensor
