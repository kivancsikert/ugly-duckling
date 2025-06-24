#pragma once

#include <chrono>
#include <list>

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

class ElectricFenceMonitor final {
public:
    ElectricFenceMonitor(
        const std::string& name,
        const std::shared_ptr<PulseCounterManager>& pulseCounterManager,
        const std::shared_ptr<ElectricFenceMonitorDeviceConfig>& config) {

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

class ElectricFenceMonitorFactory
    : public PeripheralFactory<ElectricFenceMonitorDeviceConfig, EmptyConfiguration> {
public:
    ElectricFenceMonitorFactory()
        : PeripheralFactory<ElectricFenceMonitorDeviceConfig, EmptyConfiguration>("electric-fence") {
    }

    std::shared_ptr<Peripheral<EmptyConfiguration>> createPeripheral(const std::string& name, const std::shared_ptr<ElectricFenceMonitorDeviceConfig>& deviceConfig, const std::shared_ptr<MqttRoot>& /*mqttRoot*/, const PeripheralServices& services) override {
        auto monitor = std::make_shared<ElectricFenceMonitor>(name, services.pulseCounterManager, deviceConfig);
        services.telemetryCollector->registerProvider("fence", name, [monitor](JsonObject& telemetryJson) {
            telemetryJson["voltage"] = monitor->getVoltage();
        });
        return std::make_shared<SimplePeripheral>(name, monitor);
    }
};

}    // namespace farmhub::peripherals::fence
