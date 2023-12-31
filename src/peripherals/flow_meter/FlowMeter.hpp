#pragma once

#include <memory>

#include <devices/Peripheral.hpp>
#include <kernel/Configuration.hpp>
#include <kernel/drivers/MqttDriver.hpp>
#include <peripherals/flow_meter/FlowMeterComponent.hpp>
#include <peripherals/flow_meter/FlowMeterConfig.hpp>

using namespace farmhub::devices;
using namespace farmhub::kernel;
using namespace farmhub::kernel::drivers;
using std::make_unique;
using std::unique_ptr;
namespace farmhub::peripherals::flow_meter {

class FlowMeter
    : public Peripheral<EmptyConfiguration> {
public:
    FlowMeter(const String& name, shared_ptr<MqttDriver::MqttRoot> mqttRoot, gpio_num_t pin, double qFactor, milliseconds measurementFrequency)
        : Peripheral<EmptyConfiguration>(name, mqttRoot)
        , flowMeter(name, mqttRoot, pin, qFactor, measurementFrequency) {
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

    unique_ptr<Peripheral<EmptyConfiguration>> createPeripheral(const String& name, const FlowMeterDeviceConfig& deviceConfig, shared_ptr<MqttDriver::MqttRoot> mqttRoot) override {
        return make_unique<FlowMeter>(name, mqttRoot, deviceConfig.pin.get(), deviceConfig.qFactor.get(), deviceConfig.measurementFrequency.get());
    }
};

}    // namespace farmhub::peripherals::flow_meter
