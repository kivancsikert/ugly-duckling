#pragma once

#include <chrono>

#include <Arduino.h>
#include <ArduinoJson.h>

#include <kernel/BootClock.hpp>
#include <kernel/Component.hpp>
#include <kernel/Concurrent.hpp>
#include <kernel/PulseCounter.hpp>
#include <kernel/Task.hpp>
#include <kernel/Telemetry.hpp>
#include <kernel/mqtt/MqttDriver.hpp>

using namespace farmhub::kernel::mqtt;

namespace farmhub::peripherals::flow_meter {

class FlowMeterComponent
    : public Component,
      public TelemetryProvider {
public:
    FlowMeterComponent(
        const String& name,
        shared_ptr<MqttRoot> mqttRoot,
        InternalPinPtr pin,
        double qFactor,
        milliseconds measurementFrequency,
        microseconds glitchDuration)
        : Component(name, mqttRoot)
        , qFactor(qFactor) {

        LOGI("Initializing flow meter on pin %s with Q = %.2f (glitch duration = %lld us)",
            pin->getName().c_str(), qFactor, glitchDuration.count());

        counter = make_shared<PulseCounter>(pin, 10us);

        auto now = boot_clock::now();
        lastMeasurement = now;
        lastSeenFlow = now;
        lastPublished = now;

        Task::loop(name, 3172, [this, measurementFrequency](Task& task) {
            auto now = boot_clock::now();
            milliseconds elapsed = duration_cast<milliseconds>(now - lastMeasurement);
            if (elapsed.count() > 0) {
                lastMeasurement = now;

                uint32_t pulses = counter->reset();

                if (pulses > 0) {
                    Lock lock(updateMutex);
                    double currentVolume = pulses / this->qFactor / 60.0f;
                    LOGV("Counted %lu pulses, %.2f l/min, %.2f l",
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

    shared_ptr<PulseCounter> counter;
    const double qFactor;

    time_point<boot_clock> lastMeasurement;
    time_point<boot_clock> lastSeenFlow;
    time_point<boot_clock> lastPublished;
    double volume = 0.0;

    Mutex updateMutex;
};

}    // namespace farmhub::peripherals::flow_meter
