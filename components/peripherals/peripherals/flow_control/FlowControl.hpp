#pragma once

#include <memory>
#include <utility>

#include <Configuration.hpp>
#include <mqtt/MqttDriver.hpp>
#include <peripherals/Motorized.hpp>
#include <peripherals/Peripheral.hpp>
#include <peripherals/flow_meter/FlowMeter.hpp>
#include <peripherals/valve/Valve.hpp>
#include <peripherals/valve/ValveConfig.hpp>

using namespace farmhub::kernel::mqtt;
using namespace farmhub::peripherals;
using namespace farmhub::peripherals::flow_meter;
using namespace farmhub::peripherals::valve;

namespace farmhub::peripherals::flow_control {

class FlowControlConfig
    : public ValveConfig {
};

class FlowControlFactory;

class FlowControl : public Peripheral<FlowControlConfig> {
public:
    FlowControl(
        const std::string& name,
        const std::shared_ptr<Valve>& valve,
        const std::shared_ptr<FlowMeter>& flowMeter)
        : Peripheral<FlowControlConfig>(name)
        , valve(valve)
        , flowMeter(flowMeter) {
    }

    void configure(const std::shared_ptr<FlowControlConfig> config) override {
        valve->configure(config->schedule.get(), config->overrideState.get(), config->overrideUntil.get());
    }

    void shutdown(const ShutdownParameters /*parameters*/) override {
        valve->closeBeforeShutdown();
    }

private:
    std::shared_ptr<Valve> valve;
    std::shared_ptr<FlowMeter> flowMeter;

    friend class FlowControlFactory;
};

class FlowControlSettings
    : public ConfigurationSection {
public:
    explicit FlowControlSettings(ValveControlStrategyType defaultStrategy)
        : valve(this, "valve", defaultStrategy)
        , flowMeter(this, "flow-meter") {
    }

    NamedConfigurationEntry<ValveSettings> valve;
    NamedConfigurationEntry<FlowMeterSettings> flowMeter;
};

class FlowControlFactory
    : public PeripheralFactory<FlowControlSettings, FlowControlConfig, ValveControlStrategyType>,
      protected Motorized {
public:
    FlowControlFactory(
        const std::map<std::string, std::shared_ptr<PwmMotorDriver>>& motors,
        ValveControlStrategyType defaultStrategy)
        : PeripheralFactory<FlowControlSettings, FlowControlConfig, ValveControlStrategyType>("flow-control", defaultStrategy)
        , Motorized(motors) {
    }

    std::shared_ptr<Peripheral<FlowControlConfig>> createPeripheral(const std::string& name, const std::shared_ptr<FlowControlSettings>& settings, const std::shared_ptr<MqttRoot>& mqttRoot, const PeripheralServices& services) override {
        auto strategy = settings->valve.get()->createValveControlStrategy(this);

        auto valve = std::make_shared<Valve>(
            name,
            std::move(strategy),
            mqttRoot,
            services.telemetryPublisher);

        auto flowMeterConfig = settings->flowMeter.get();
        auto flowMeter = std::make_shared<FlowMeter>(
            name,
            services.pulseCounterManager,
            flowMeterConfig->pin.get(),
            flowMeterConfig->qFactor.get(),
            flowMeterConfig->measurementFrequency.get());

        services.telemetryCollector->registerFeature("valve", name, [valve](JsonObject& telemetry) {
            valve->populateTelemetry(telemetry);
        });
        services.telemetryCollector->registerFeature("flow", name, [flowMeter](JsonObject& telemetry) {
            flowMeter->populateTelemetry(telemetry);
        });

        return std::make_shared<FlowControl>(name, valve, flowMeter);
    }
};

}    // namespace farmhub::peripherals::flow_control
