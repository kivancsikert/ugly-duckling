#pragma once

#include <devices/Peripheral.hpp>
#include <kernel/Configuration.hpp>
#include <kernel/drivers/MqttDriver.hpp>
#include <peripherals/flow_meter/FlowMeterComponent.hpp>
#include <peripherals/flow_meter/FlowMeterConfig.hpp>

using namespace farmhub::devices;
using namespace farmhub::kernel;
using namespace farmhub::kernel::drivers;

namespace farmhub { namespace peripherals { namespace flow_meter {

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

    FlowMeterDeviceConfig* createDeviceConfig() override {
        return new FlowMeterDeviceConfig();
    }

    FlowMeter* createPeripheral(const String& name, const FlowMeterDeviceConfig& deviceConfig, shared_ptr<MqttDriver::MqttRoot> mqttRoot) override {
        return new FlowMeter(name, mqttRoot, deviceConfig.pin.get(), deviceConfig.qFactor.get(), deviceConfig.measurementFrequency.get());
    }
};

}}}    // namespace farmhub::peripherals::flow_meter
