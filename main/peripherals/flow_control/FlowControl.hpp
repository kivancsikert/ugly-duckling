#pragma once

#include <memory>

#include <kernel/Configuration.hpp>
#include <kernel/PcntManager.hpp>
#include <kernel/SleepManager.hpp>
#include <kernel/mqtt/MqttDriver.hpp>
#include <peripherals/Motorized.hpp>
#include <peripherals/Peripheral.hpp>
#include <peripherals/flow_meter/FlowMeterComponent.hpp>
#include <peripherals/flow_meter/FlowMeterConfig.hpp>
#include <peripherals/valve/Valve.hpp>
#include <peripherals/valve/ValveComponent.hpp>
#include <peripherals/valve/ValveConfig.hpp>

using namespace farmhub::kernel::mqtt;
using namespace farmhub::peripherals;
using namespace farmhub::peripherals::flow_meter;
using namespace farmhub::peripherals::valve;
using std::make_unique;
using std::unique_ptr;

namespace farmhub::peripherals::flow_control {

class FlowControlConfig
    : public ValveConfig {
};

class FlowControl : public Peripheral<FlowControlConfig> {
public:
    FlowControl(
        const String& name,
        shared_ptr<MqttRoot> mqttRoot,
        PcntManager& pcnt,
        SleepManager& sleepManager,
        ValveControlStrategy& strategy,
        InternalPinPtr pin,
        double qFactor,
        milliseconds measurementFrequency)
        : Peripheral<FlowControlConfig>(name, mqttRoot)
        , valve(name, sleepManager, strategy, mqttRoot, [this]() {
            publishTelemetry();
        })
        , flowMeter(name, mqttRoot, pcnt, pin, qFactor, measurementFrequency) {
    }

    void configure(const FlowControlConfig& config) override {
        valve.setSchedules(config.schedule.get());
    }

    void populateTelemetry(JsonObject& telemetryJson) override {
        valve.populateTelemetry(telemetryJson);
        flowMeter.populateTelemetry(telemetryJson);
    }

    void shutdown(const ShutdownParameters parameters) override {
        valve.closeBeforeShutdown();
    }

private:
    ValveComponent valve;
    FlowMeterComponent flowMeter;
};

class FlowControlDeviceConfig
    : public ConfigurationSection {
public:
    FlowControlDeviceConfig(ValveControlStrategyType defaultStrategy)
        : valve(this, "valve", defaultStrategy)
        , flowMeter(this, "flow-meter") {
    }

    NamedConfigurationEntry<ValveDeviceConfig> valve;
    NamedConfigurationEntry<FlowMeterDeviceConfig> flowMeter;
};

class FlowControlFactory
    : public PeripheralFactory<FlowControlDeviceConfig, FlowControlConfig, ValveControlStrategyType>,
      protected Motorized {
public:
    FlowControlFactory(
        const std::list<ServiceRef<PwmMotorDriver>>& motors,
        ValveControlStrategyType defaultStrategy)
        : PeripheralFactory<FlowControlDeviceConfig, FlowControlConfig, ValveControlStrategyType>("flow-control", defaultStrategy)
        , Motorized(motors) {
    }

    unique_ptr<Peripheral<FlowControlConfig>> createPeripheral(const String& name, const FlowControlDeviceConfig& deviceConfig, shared_ptr<MqttRoot> mqttRoot, PeripheralServices& services) override {
        auto strategy = deviceConfig.valve.get().createValveControlStrategy(this);

        auto flowMeterConfig = deviceConfig.flowMeter.get();
        return make_unique<FlowControl>(
            name,
            mqttRoot,

            services.pcntManager,
            services.sleepManager,
            *strategy,

            flowMeterConfig.pin.get(),
            flowMeterConfig.qFactor.get(),
            flowMeterConfig.measurementFrequency.get());
    }

private:
    const std::list<ServiceRef<PwmMotorDriver>> motors;
};

}    // namespace farmhub::peripherals::flow_control
