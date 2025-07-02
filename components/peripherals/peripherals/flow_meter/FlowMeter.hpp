#pragma once

#include <chrono>

#include <ArduinoJson.h>

#include <BootClock.hpp>
#include <Concurrent.hpp>
#include <PulseCounter.hpp>
#include <Task.hpp>
#include <Telemetry.hpp>
#include <mqtt/MqttDriver.hpp>
#include <utility>

using namespace farmhub::kernel::mqtt;

namespace farmhub::peripherals::flow_meter {

class FlowMeterSettings
    : public ConfigurationSection {
public:
    Property<InternalPinPtr> pin { this, "pin" };
    // Default Q factor for YF-S201 flow sensor
    Property<double> qFactor { this, "qFactor", 7.5 };
    Property<milliseconds> measurementFrequency { this, "measurementFrequency", 1s };
};

class FlowMeter {
public:
    FlowMeter(
        const std::string& name,
        const std::shared_ptr<PulseCounterManager>& pulseCounterManager,
        const InternalPinPtr& pin,
        double qFactor,
        milliseconds measurementFrequency)
        : qFactor(qFactor) {

        LOGI("Initializing flow meter on pin %s with Q = %.2f",
            pin->getName().c_str(), qFactor);

        counter = pulseCounterManager->create(pin);

        auto now = boot_clock::now();
        lastMeasurement = now;
        lastSeenFlow = now;
        lastPublished = now;

        Task::loop(name, 3072, [this, measurementFrequency](Task& task) {
            auto now = boot_clock::now();
            milliseconds elapsed = duration_cast<milliseconds>(now - lastMeasurement);
            if (elapsed.count() > 0) {
                lastMeasurement = now;

                uint32_t pulses = counter->reset();

                if (pulses > 0) {
                    Lock lock(updateMutex);
                    double currentVolume = pulses / this->qFactor / 60.0F;
                    LOGV("Counted %" PRIu32 " pulses, %.2f l/min, %.2f l",
                        pulses, currentVolume / (elapsed.count() / 1000.0F / 60.0F), currentVolume);
                    volume += currentVolume;
                    lastSeenFlow = now;
                }
            }
            task.delayUntil(measurementFrequency);
        });
    }

    void populateTelemetry(JsonObject& json) {
        Lock lock(updateMutex);
        populateTelemetryUnderLock(json);
    }

private:
    void populateTelemetryUnderLock(JsonObject& json) {
        auto currentVolume = volume;
        volume = 0;
        // Volume is measured in liters
        json["volume"] = currentVolume;
        auto duration = duration_cast<microseconds>(lastMeasurement - lastPublished);
        if (duration > microseconds::zero()) {
            // Flow rate is measured in in liters / min
            json["rate"] = currentVolume / static_cast<double>(duration.count()) * 1000 * 1000 * 60;
        }
        lastPublished = lastMeasurement;
    }

    std::shared_ptr<PulseCounter> counter;
    const double qFactor;

    time_point<boot_clock> lastMeasurement;
    time_point<boot_clock> lastSeenFlow;
    time_point<boot_clock> lastPublished;
    double volume = 0.0;

    Mutex updateMutex;
};

class FlowMeterFactory
    : public PeripheralFactory<FlowMeterSettings> {
public:
    FlowMeterFactory()
        : PeripheralFactory<FlowMeterSettings>("flow-meter") {
    }

    std::shared_ptr<Peripheral<EmptyConfiguration>> createPeripheral(PeripheralInitParameters& params, const std::shared_ptr<FlowMeterSettings>& settings) override {
        auto meter = std::make_shared<FlowMeter>(params.name, params.services.pulseCounterManager, settings->pin.get(), settings->qFactor.get(), settings->measurementFrequency.get());
        params.registerFeature("flow", [meter](JsonObject& telemetry) {
            meter->populateTelemetry(telemetry);
        });
        return std::make_shared<SimplePeripheral>(params.name, meter);
    }
};

}    // namespace farmhub::peripherals::flow_meter
