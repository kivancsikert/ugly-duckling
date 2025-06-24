#pragma once

#include <chrono>
#include <list>

#include <Component.hpp>
#include <Concurrent.hpp>
#include <PulseCounter.hpp>
#include <Telemetry.hpp>

#include <peripherals/Peripheral.hpp>
#include <utility>

using namespace std::chrono_literals;
using namespace farmhub::kernel;
using namespace farmhub::peripherals;

namespace farmhub::peripherals::fence {

class ElectricFenceMonitorFactory;

struct FencePinConfig {
    InternalPinPtr pin;
    uint16_t voltage {};
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

class ElectricFenceMonitorComponent final
    : public Component {
public:
    ElectricFenceMonitorComponent(
        const std::string& name,
        std::shared_ptr<MqttRoot> mqttRoot,
        const std::shared_ptr<PulseCounterManager>& pulseCounterManager,
        const std::shared_ptr<ElectricFenceMonitorDeviceConfig>& config)
        : Component(name, std::move(mqttRoot)) {

        std::string pinsDescription;
        for (const auto& pinConfig : config->pins.get()) {
            if (!pinsDescription.empty()) {
                pinsDescription += ", ";
            }
            pinsDescription += pinConfig.pin->getName() + "=" + std::to_string(pinConfig.voltage) + "V";
        }
        LOGI("Initializing electric fence with pins %s", pinsDescription.c_str());

        for (const auto& pinConfig : config->pins.get()) {
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
                    LOGV("Counted %" PRIu32 " pulses on pin %s (voltage: %dV)",
                        count, pin.counter->getPin()->getName().c_str(), pin.voltage);
                }
            }
            this->lastVoltage = lastVoltage;
            LOGV("Last voltage: %d",
                lastVoltage);
            task.delayUntil(measurementFrequency);
        });
    }

    double getVoltage() const {
        return lastVoltage.load();
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
        const std::shared_ptr<MqttRoot>& mqttRoot,
        const std::shared_ptr<PulseCounterManager>& pulseCounterManager,
        const std::shared_ptr<ElectricFenceMonitorDeviceConfig>& config)
        : Peripheral<EmptyConfiguration>(name, mqttRoot)
        , monitor(name, mqttRoot, pulseCounterManager, config) {
    }

private:
    ElectricFenceMonitorComponent monitor;
    friend class ElectricFenceMonitorFactory;
};

class ElectricFenceMonitorFactory
    : public PeripheralFactory<ElectricFenceMonitorDeviceConfig, EmptyConfiguration> {
public:
    ElectricFenceMonitorFactory()
        : PeripheralFactory<ElectricFenceMonitorDeviceConfig, EmptyConfiguration>("electric-fence") {
    }

    std::shared_ptr<Peripheral<EmptyConfiguration>> createPeripheral(const std::string& name, const std::shared_ptr<ElectricFenceMonitorDeviceConfig> deviceConfig, std::shared_ptr<MqttRoot> mqttRoot, const PeripheralServices& services) override {
        auto peripheral = std::make_shared<ElectricFenceMonitor>(name, mqttRoot, services.pulseCounterManager, deviceConfig);
        services.telemetryCollector->registerProvider("fence", name, [peripheral](JsonObject& telemetryJson) {
            telemetryJson["voltage"] = peripheral->monitor.getVoltage();
        });
        return peripheral;
    }
};

}    // namespace farmhub::peripherals::fence
