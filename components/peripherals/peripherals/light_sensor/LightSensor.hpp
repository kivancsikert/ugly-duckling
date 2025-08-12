#pragma once

#include <chrono>
#include <memory>

#include <Configuration.hpp>
#include <I2CManager.hpp>
#include <MovingAverage.hpp>
#include <Named.hpp>

#include <peripherals/I2CSettings.hpp>
#include <peripherals/Peripheral.hpp>
#include <utility>

using namespace std::chrono;
using namespace std::chrono_literals;

using namespace farmhub::kernel;
using namespace farmhub::peripherals;

namespace farmhub::peripherals::light_sensor {

class LightSensor
    : Named {
public:
    LightSensor(
        const std::string& name,
        seconds measurementFrequency,
        seconds latencyInterval)
        : Named(name)
        , measurementFrequency(measurementFrequency)
        , level(latencyInterval.count() / measurementFrequency.count()) {
    }

    virtual ~LightSensor() = default;

    double getCurrentLevel() {
        Lock lock(updateAverageMutex);
        return level.getAverage();
    }

    seconds getMeasurementFrequency() {
        return measurementFrequency;
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
