#pragma once

#include <memory>

#include <Configuration.hpp>
#include <mqtt/MqttDriver.hpp>
#include <peripherals/Motorized.hpp>
#include <peripherals/Peripheral.hpp>
#include <peripherals/flow_meter/FlowMeterComponent.hpp>
#include <peripherals/flow_meter/FlowMeterConfig.hpp>
#include <peripherals/valve/Valve.hpp>
#include <peripherals/valve/ValveComponent.hpp>
#include <peripherals/valve/ValveConfig.hpp>
#include <utility>

using namespace farmhub::kernel::mqtt;
using namespace farmhub::peripherals;
using namespace farmhub::peripherals::flow_meter;
using namespace farmhub::peripherals::valve;

namespace farmhub::peripherals::flow_control {

class FlowControlConfig
    : public ValveConfig {
};

class FlowControl : public Peripheral<FlowControlConfig> {
public:
    FlowControl(
        const std::string& name,
        const std::shared_ptr<MqttRoot>& mqttRoot,
        const std::shared_ptr<PulseCounterManager>& pulseCounterManager,
        std::unique_ptr<ValveControlStrategy> strategy,
        const InternalPinPtr& pin,
        double qFactor,
        milliseconds measurementFrequency)
        : Peripheral<FlowControlConfig>(name, mqttRoot)
        , valve(name, std::move(strategy), mqttRoot, [this]() {
            publishTelemetry();
        })
        , flowMeter(name, mqttRoot, pulseCounterManager, pin, qFactor, measurementFrequency) {
    }

    void configure(const std::shared_ptr<FlowControlConfig> config) override {
        valve.setSchedules(config->schedule.get());
    }

    void populateTelemetry(JsonObject& telemetryJson) override {
        valve.populateTelemetry(telemetryJson);
        flowMeter.populateTelemetry(telemetryJson);
    }

    void shutdown(const ShutdownParameters /*parameters*/) override {
        valve.closeBeforeShutdown();
    }

private:
    ValveComponent valve;
    FlowMeterComponent flowMeter;
};

class FlowControlDeviceConfig
    : public ConfigurationSection {
public:
    explicit FlowControlDeviceConfig(ValveControlStrategyType defaultStrategy)
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
        const std::map<std::string, std::shared_ptr<PwmMotorDriver>>& motors,
        ValveControlStrategyType defaultStrategy)
        : PeripheralFactory<FlowControlDeviceConfig, FlowControlConfig, ValveControlStrategyType>("flow-control", defaultStrategy)
        , Motorized(motors) {
    }

    std::unique_ptr<Peripheral<FlowControlConfig>> createPeripheral(const std::string& name, const std::shared_ptr<FlowControlDeviceConfig> deviceConfig, std::shared_ptr<MqttRoot> mqttRoot, const PeripheralServices& services) override {
        auto strategy = deviceConfig->valve.get()->createValveControlStrategy(this);

        auto flowMeterConfig = deviceConfig->flowMeter.get();
        return std::make_unique<FlowControl>(
            name,
            mqttRoot,
            services.pulseCounterManager,

            std::move(strategy),

            flowMeterConfig->pin.get(),
            flowMeterConfig->qFactor.get(),
            flowMeterConfig->measurementFrequency.get());
    }
};

}    // namespace farmhub::peripherals::flow_control
