#pragma once

#include <chrono>

#include <driver/pcnt.h>

#include <Arduino.h>
#include <ArduinoJson.h>
#include <ArduinoLog.h>

#include <kernel/BootClock.hpp>
#include <kernel/Component.hpp>
#include <kernel/Concurrent.hpp>
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
        gpio_num_t pin,
        double qFactor,
        milliseconds measurementFrequency)
        : Component(name, mqttRoot)
        , pin(pin)
        , qFactor(qFactor) {

        Log.infoln("Initializing flow meter on pin %d with Q = %F", pin, qFactor);

        pinMode(pin, INPUT);

        // TODO Manage PCNT globally
        pcnt_config_t pcntConfig = {};
        pcntConfig.pulse_gpio_num = pin;
        pcntConfig.ctrl_gpio_num = PCNT_PIN_NOT_USED;
        pcntConfig.lctrl_mode = PCNT_MODE_KEEP;
        pcntConfig.hctrl_mode = PCNT_MODE_KEEP;
        pcntConfig.pos_mode = PCNT_COUNT_INC;
        pcntConfig.neg_mode = PCNT_COUNT_DIS;
        pcntConfig.unit = PCNT_UNIT_0;
        pcntConfig.channel = PCNT_CHANNEL_0;

        pcnt_unit_config(&pcntConfig);
        pcnt_intr_disable(PCNT_UNIT_0);
        pcnt_set_filter_value(PCNT_UNIT_0, 1023);
        pcnt_filter_enable(PCNT_UNIT_0);
        pcnt_counter_clear(PCNT_UNIT_0);

        auto now = boot_clock::now();
        lastMeasurement = now;
        lastSeenFlow = now;
        lastPublished = now;

        Task::loop(name, 2560, [this, measurementFrequency](Task& task) {
            auto now = boot_clock::now();
            milliseconds elapsed = duration_cast<milliseconds>(now - lastMeasurement);
            if (elapsed.count() > 0) {
                lastMeasurement = now;

                int16_t pulses;
                pcnt_get_counter_value(PCNT_UNIT_0, &pulses);

                if (pulses > 0) {
                    pcnt_counter_clear(PCNT_UNIT_0);
                    Lock lock(updateMutex);
                    double currentVolume = pulses / this->qFactor / 60.0f;
                    Log.verboseln("Counted %d pulses, %F l/min, %F l",
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

    const gpio_num_t pin;
    const double qFactor;

    time_point<boot_clock> lastMeasurement;
    time_point<boot_clock> lastSeenFlow;
    time_point<boot_clock> lastPublished;
    double volume = 0.0;

    Mutex updateMutex;
};

}    // namespace farmhub::peripherals::flow_meter
