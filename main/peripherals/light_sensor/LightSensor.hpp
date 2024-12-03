#pragma once

#include <chrono>
#include <deque>
#include <memory>

#include <Arduino.h>
#include <Wire.h>

#include <BH1750.h>

#include <kernel/Component.hpp>
#include <kernel/Configuration.hpp>
#include <kernel/I2CManager.hpp>
#include <kernel/MovingAverage.hpp>
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
        , level(latencyInterval.count() / measurementFrequency.count()) {
    }

    double getCurrentLevel() {
        Lock lock(updateAverageMutex);
        return level.getAverage();
    }

    seconds getMeasurementFrequency() {
        return measurementFrequency;
    }

    void populateTelemetry(JsonObject& json) override {
        Lock lock(updateAverageMutex);
        json["light"] = level.getAverage();
    }

protected:
    virtual double readLightLevel() = 0;

    void runLoop() {
        Task::loop(name, 3072, [this](Task& task) {
            auto currentLevel = readLightLevel();
            {
                Lock lock(updateAverageMutex);
                level.record(currentLevel);
            }
            task.delayUntil(measurementFrequency);
        });
    }

private:
    const seconds measurementFrequency;
    Mutex updateAverageMutex;
    MovingAverage<double> level;
};

}    // namespace farmhub::peripherals::light_sensor
