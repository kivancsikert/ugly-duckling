#pragma once

#include <memory>

#include <Configuration.hpp>
#include <mqtt/MqttDriver.hpp>
#include <peripherals/Peripheral.hpp>
#include <peripherals/flow_meter/FlowMeterComponent.hpp>
#include <peripherals/flow_meter/FlowMeterConfig.hpp>
#include <utility>

using namespace farmhub::kernel;
using namespace farmhub::kernel::mqtt;
using namespace farmhub::peripherals;

namespace farmhub::peripherals::flow_meter {

class FlowMeterFactory;

class FlowMeter
    : public Peripheral<EmptyConfiguration> {
public:
    FlowMeter(
        const std::string& name,
        const std::shared_ptr<PulseCounterManager>& pulseCounterManager,
        const InternalPinPtr& pin,
        double qFactor,
        milliseconds measurementFrequency)
        : Peripheral<EmptyConfiguration>(name)
        , flowMeter(name, pulseCounterManager, pin, qFactor, measurementFrequency) {
    }

private:
    FlowMeterComponent flowMeter;
    friend class FlowMeterFactory;
};

class FlowMeterFactory
    : public PeripheralFactory<FlowMeterDeviceConfig, EmptyConfiguration> {
public:
    FlowMeterFactory()
        : PeripheralFactory<FlowMeterDeviceConfig, EmptyConfiguration>("flow-meter") {
    }

    std::shared_ptr<Peripheral<EmptyConfiguration>> createPeripheral(const std::string& name, const std::shared_ptr<FlowMeterDeviceConfig> deviceConfig, std::shared_ptr<MqttRoot>  /*mqttRoot*/, const PeripheralServices& services) override {
        auto peripheral = std::make_shared<FlowMeter>(name, services.pulseCounterManager, deviceConfig->pin.get(), deviceConfig->qFactor.get(), deviceConfig->measurementFrequency.get());
        services.telemetryCollector->registerProvider("flow", name, [peripheral](JsonObject& telemetry) {
            peripheral->flowMeter.populateTelemetry(telemetry);
        });
        return peripheral;
    }
};

}    // namespace farmhub::peripherals::flow_meter
