#pragma once

#include <chrono>
#include <memory>

#include <Component.hpp>
#include <Configuration.hpp>
#include <I2CManager.hpp>
#include <MovingAverage.hpp>

#include <peripherals/I2CConfig.hpp>
#include <peripherals/Peripheral.hpp>
#include <utility>

using namespace std::chrono;
using namespace std::chrono_literals;

using namespace farmhub::kernel;
using namespace farmhub::peripherals;

namespace farmhub::peripherals::light_sensor {

class LightSensorComponent
    : public Component {
public:
    LightSensorComponent(
        const std::string& name,
        std::shared_ptr<MqttRoot> mqttRoot,
        seconds measurementFrequency,
        seconds latencyInterval)
        : Component(name, std::move(mqttRoot))
        , measurementFrequency(measurementFrequency)
        , level(latencyInterval.count() / measurementFrequency.count()) {
    }

    virtual ~LightSensorComponent() = default;

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
