#pragma once

#include <chrono>
#include <list>

#include <Component.hpp>
#include <Concurrent.hpp>
#include <PulseCounter.hpp>
#include <Telemetry.hpp>

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
        const std::string& name,
        std::shared_ptr<MqttRoot> mqttRoot,
        std::shared_ptr<PulseCounterManager> pulseCounterManager,
        const std::shared_ptr<ElectricFenceMonitorDeviceConfig> config)
        : Component(name, mqttRoot) {

        std::string pinsDescription;
        for (auto& pinConfig : config->pins.get()) {
            if (pinsDescription.length() > 0)
                pinsDescription += ", ";
            pinsDescription += pinConfig.pin->getName() + "=" + std::to_string(pinConfig.voltage) + "V";
        }
        LOGI("Initializing electric fence with pins %s", pinsDescription.c_str());

        for (auto& pinConfig : config->pins.get()) {
            auto unit = pulseCounterManager->create(pinConfig.pin);
            pins.emplace_back(pinConfig.voltage, unit);
        }

        auto measurementFrequency = config->measurementFrequency.get();
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
        std::shared_ptr<PulseCounter> counter;
    };

    std::list<FencePin> pins;
};

class ElectricFenceMonitor
    : public Peripheral<EmptyConfiguration> {
public:
    ElectricFenceMonitor(
        const std::string& name,
        std::shared_ptr<MqttRoot> mqttRoot,
        std::shared_ptr<PulseCounterManager> pulseCounterManager,
        const std::shared_ptr<ElectricFenceMonitorDeviceConfig> config)
        : Peripheral<EmptyConfiguration>(name, mqttRoot)
        , monitor(name, mqttRoot, pulseCounterManager, config) {
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

    std::unique_ptr<Peripheral<EmptyConfiguration>> createPeripheral(const std::string& name, const std::shared_ptr<ElectricFenceMonitorDeviceConfig> deviceConfig, std::shared_ptr<MqttRoot> mqttRoot, const PeripheralServices& services) override {
        return std::make_unique<ElectricFenceMonitor>(name, mqttRoot, services.pulseCounterManager, deviceConfig);
    }
};

}    // namespace farmhub::peripherals::fence
