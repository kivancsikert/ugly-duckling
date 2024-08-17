#pragma once

#include <chrono>
#include <vector>
#include <stdexcept>
#include <algorithm>
#include <memory>

#include <Arduino.h>
#include <ArduinoJson.h>

#include <kernel/BootClock.hpp>
#include <kernel/Component.hpp>
#include <kernel/Concurrent.hpp>
#include <kernel/Log.hpp>
#include <kernel/PcntManager.hpp>
#include <kernel/Task.hpp>
#include <kernel/Telemetry.hpp>

#include <kernel/drivers/MqttDriver.hpp>

using namespace farmhub::kernel::drivers;

namespace farmhub::peripherals::flow_meter {

class FlowMeterComponent
    : public Component,
      public TelemetryProvider {
public:
    FlowMeterComponent(
        const String& name,
        shared_ptr<MqttDriver::MqttRoot> mqttRoot,
        PcntManager& pcnt,
        gpio_num_t pin,
        double qFactor,
        milliseconds measurementFrequency)
        : Component(name, mqttRoot)
        , qFactor(qFactor) {

        Log.info("Initializing flow meter on pin %d with Q = %.2f", pin, qFactor);

        pcntUnit = pcnt.registerUnit(pin);

        auto now = boot_clock::now();
        lastMeasurement = now;
        lastSeenFlow = now;
        lastPublished = now;

        Task::loop(name, 2560, [this, measurementFrequency](Task& task) {
            auto now = boot_clock::now();
            milliseconds elapsed = duration_cast<milliseconds>(now - lastMeasurement);
            if (elapsed.count() > 0) {
                lastMeasurement = now;

                int16_t pulses = pcntUnit.getAndClearCount();

                if (pulses > 0) {
                    Lock lock(updateMutex);
                    double currentVolume = pulses / this->qFactor / 60.0f;
                    Log.trace("Counted %d pulses, %.2f l/min, %.2f l",
                        pulses, currentVolume / (elapsed.count() / 1000.0f / 60.0f), currentVolume);
                    volume += currentVolume;
                    lastSeenFlow = now;
                }
            }
            task.delayUntil(measurementFrequency);
        });
    }

    virtual ~FlowMeterComponent() = default;

    void populateTelemetry(JsonObject& json) override {
        Lock lock(updateMutex);
        pupulateTelemetryUnderLock(json);
    }

private:
    void inline pupulateTelemetryUnderLock(JsonObject& json) {
        auto currentVolume = volume;
        volume = 0;
        // Volume is measured in liters
        json["volume"] = currentVolume;
        auto duration = duration_cast<microseconds>(lastMeasurement - lastPublished);
        if (duration > microseconds::zero()) {
            // Flow rate is measured in in liters / min
            json["flowRate"] = currentVolume / duration.count() * 1000 * 1000 * 60;
        }
        lastPublished = lastMeasurement;
    }

    PcntUnit pcntUnit;
    const double qFactor;

    time_point<boot_clock> lastMeasurement;
    time_point<boot_clock> lastSeenFlow;
    time_point<boot_clock> lastPublished;
    double volume = 0.0;

    Mutex updateMutex;
};

struct DataPoint {
    int pulses;
    double qFactor;

    bool operator<(const DataPoint& other) const {
        return pulses < other.pulses;
    }
};

class FlowCalculator {
public:
    virtual ~FlowCalculator() = default;
    virtual double getFlowRate(int pulses) const = 0;
    virtual String describe() const = 0;

    class Builder {
    private:
        std::vector<DataPoint> dataPoints;
    public:
        Builder& addDataPoint(int pulses, double flowRate) {
            dataPoints.push_back(DataPoint{pulses, flowRate});
            return *this;
        }

        std::unique_ptr<FlowCalculator> build() {
            std::sort(dataPoints.begin(), dataPoints.end());

            switch (dataPoints.size()) {
                case 0:
                    throw std::runtime_error("No data points available for calculation.");
                case 1:
                    return std::make_unique<FixedFlowCalculator>(dataPoints.front().qFactor);
                default:
                    return std::make_unique<InterpolatingFlowCalculator>(dataPoints);
            }
        }
    };
};

class FixedFlowCalculator : public FlowCalculator {
private:
    double qFactor;

public:
    explicit FixedFlowCalculator(double rate) : qFactor(rate) {}

    double getFlowRate(int) const override {
        return qFactor;
    }
};

class InterpolatingFlowCalculator : public FlowCalculator {
private:
    std::vector<DataPoint> dataPoints;

public:
    explicit InterpolatingFlowCalculator(const std::vector<DataPoint>& points)
        : dataPoints(points) {}

    double getFlowRate(int pulses) const override {
        // Handle cases where the pulse count is outside the provided range
        if (pulses <= dataPoints.front().pulses) {
            return dataPoints.front().flowRate;
        }
        if (pulses >= dataPoints.back().pulses) {
            return dataPoints.back().flowRate;
        }

        // Find the interval [prev, next] that contains the pulses value
        auto next = std::lower_bound(dataPoints.begin(), dataPoints.end(), DataPoint{pulses, 0.0});
        auto prev = next - 1;

        // Linear interpolation
        double slope = (next->flowRate - prev->flowRate) / (next->pulses - prev->pulses);
        return prev->flowRate + slope * (pulses - prev->pulses);
    }
};

}    // namespace farmhub::peripherals::flow_meter
