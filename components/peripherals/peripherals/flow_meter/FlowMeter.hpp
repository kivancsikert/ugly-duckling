#pragma once

#include <memory>

#include <Configuration.hpp>
#include <mqtt/MqttDriver.hpp>
#include <peripherals/Peripheral.hpp>
#include <peripherals/flow_meter/FlowMeterComponent.hpp>
#include <peripherals/flow_meter/FlowMeterConfig.hpp>

using namespace farmhub::kernel;
using namespace farmhub::kernel::mqtt;
using namespace farmhub::peripherals;

namespace farmhub::peripherals::flow_meter {

class FlowMeter
    : public Peripheral<EmptyConfiguration> {
public:
    FlowMeter(
        const std::string& name,
        std::shared_ptr<MqttRoot> mqttRoot,
        std::shared_ptr<PulseCounterManager> pulseCounterManager,
        InternalPinPtr pin,
        double qFactor,
        milliseconds measurementFrequency)
        : Peripheral<EmptyConfiguration>(name, mqttRoot)
        , flowMeter(name, mqttRoot, pulseCounterManager, pin, qFactor, measurementFrequency) {
    }

    void populateTelemetry(JsonObject& telemetryJson) override {
        flowMeter.populateTelemetry(telemetryJson);
    }

private:
    FlowMeterComponent flowMeter;
};

class FlowMeterFactory
    : public PeripheralFactory<FlowMeterDeviceConfig, EmptyConfiguration> {
public:
    FlowMeterFactory()
        : PeripheralFactory<FlowMeterDeviceConfig, EmptyConfiguration>("flow-meter") {
    }

    std::unique_ptr<Peripheral<EmptyConfiguration>> createPeripheral(const std::string& name, const std::shared_ptr<FlowMeterDeviceConfig> deviceConfig, std::shared_ptr<MqttRoot> mqttRoot, const PeripheralServices& services) override {
        return std::make_unique<FlowMeter>(name, mqttRoot, services.pulseCounterManager, deviceConfig->pin.get(), deviceConfig->qFactor.get(), deviceConfig->measurementFrequency.get());
    }
};

}    // namespace farmhub::peripherals::flow_meter
