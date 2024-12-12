#pragma once

#include <chrono>
#include <list>

#include <Arduino.h>

#include <kernel/Component.hpp>
#include <kernel/Concurrent.hpp>
#include <kernel/PulseCounter.hpp>
#include <kernel/Telemetry.hpp>

#include <peripherals/Peripheral.hpp>

using namespace std::chrono_literals;
using namespace farmhub::kernel;
using namespace farmhub::peripherals;

namespace farmhub::peripherals::fence {

struct FencePinConfig {
    InternalPinPtr pin;
    uint16_t voltage;
};

class ElectricFenceMonitorDeviceConfig
    : public ConfigurationSection {
public:
    ArrayProperty<FencePinConfig> pins { this, "pins" };
    Property<seconds> measurementFrequency { this, "measurementFrequency", 10s };
};

bool convertToJson(const FencePinConfig& src, JsonVariant dst) {
    dst["pin"] = src.pin;
    dst["voltage"] = src.voltage;
    return true;
}
void convertFromJson(JsonVariantConst src, FencePinConfig& dst) {
    dst.pin = src["pin"];
    dst.voltage = src["voltage"];
}

class ElectricFenceMonitorComponent
    : public Component,
      public TelemetryProvider {
public:
    ElectricFenceMonitorComponent(
        const String& name,
        shared_ptr<MqttRoot> mqttRoot,
        const ElectricFenceMonitorDeviceConfig& config)
        : Component(name, mqttRoot) {

        String pinsDescription;
        for (auto& pinConfig : config.pins.get()) {
            if (pinsDescription.length() > 0)
                pinsDescription += ", ";
            pinsDescription += pinConfig.pin->getName() + "=" + String(pinConfig.voltage) + "V";
        }
        LOGI("Initializing electric fence with pins %s", pinsDescription.c_str());

        for (auto& pinConfig : config.pins.get()) {
            auto unit = make_shared<PulseCounter>(pinConfig.pin);
            pins.emplace_back(pinConfig.voltage, unit);
        }

        auto measurementFrequency = config.measurementFrequency.get();
        Task::loop(name, 3172, [this, measurementFrequency](Task& task) {
            uint16_t lastVoltage = 0;
            for (auto& pin : pins) {
                uint32_t count = pin.counter->reset();

                if (count > 0) {
                    lastVoltage = std::max(pin.voltage, lastVoltage);
                    LOGV("Counted %ld pulses on pin %s (voltage: %dV)",
                        count, pin.counter->getPin()->getName().c_str(), pin.voltage);
                }
            }
            this->lastVoltage = lastVoltage;
            LOGV("Last voltage: %d",
                lastVoltage);
            task.delayUntil(measurementFrequency);
        });
    }

    void populateTelemetry(JsonObject& json) override {
        json["voltage"] = lastVoltage.load();
    }

private:
    std::atomic<uint16_t> lastVoltage { 0 };

    struct FencePin {
        uint16_t voltage;
        shared_ptr<PulseCounter> counter;
    };

    std::list<FencePin> pins;
};

class ElectricFenceMonitor
    : public Peripheral<EmptyConfiguration> {
public:
    ElectricFenceMonitor(const String& name, shared_ptr<MqttRoot> mqttRoot, const ElectricFenceMonitorDeviceConfig& config)
        : Peripheral<EmptyConfiguration>(name, mqttRoot)
        , monitor(name, mqttRoot, config) {
    }

    void populateTelemetry(JsonObject& telemetryJson) override {
        monitor.populateTelemetry(telemetryJson);
    }

private:
    ElectricFenceMonitorComponent monitor;
};

class ElectricFenceMonitorFactory
    : public PeripheralFactory<ElectricFenceMonitorDeviceConfig, EmptyConfiguration> {
public:
    ElectricFenceMonitorFactory()
        : PeripheralFactory<ElectricFenceMonitorDeviceConfig, EmptyConfiguration>("electric-fence") {
    }

    unique_ptr<Peripheral<EmptyConfiguration>> createPeripheral(const String& name, const ElectricFenceMonitorDeviceConfig& deviceConfig, shared_ptr<MqttRoot> mqttRoot, PeripheralServices& services) override {
        return std::make_unique<ElectricFenceMonitor>(name, mqttRoot, deviceConfig);
    }
};

}    // namespace farmhub::peripherals::fence
