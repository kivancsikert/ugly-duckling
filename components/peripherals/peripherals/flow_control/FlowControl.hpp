#pragma once

#include <memory>
#include <utility>

#include <Configuration.hpp>
#include <Named.hpp>
#include <mqtt/MqttDriver.hpp>
#include <peripherals/Motors.hpp>
#include <peripherals/Peripheral.hpp>
#include <peripherals/flow_meter/FlowMeter.hpp>
#include <peripherals/valve/Valve.hpp>
#include <peripherals/valve/ValveConfig.hpp>

using namespace farmhub::kernel::mqtt;
using namespace farmhub::peripherals;
using namespace farmhub::peripherals::flow_meter;
using namespace farmhub::peripherals::valve;

namespace farmhub::peripherals::flow_control {

class FlowControlConfig : public ValveConfig { };

class FlowControl
    : public Named,
      public HasConfig<FlowControlConfig>,
      public HasShutdown {
public:
    FlowControl(
        const std::string& name,
        const std::shared_ptr<Valve>& valve,
        const std::shared_ptr<FlowMeter>& flowMeter)
        : Named(name)
        , valve(valve)
        , flowMeter(flowMeter) {
    }

    void configure(const std::shared_ptr<FlowControlConfig>& config) override {
        valve->configure(config->schedule.get(), config->overrideState.get(), config->overrideUntil.get());
    }

    void shutdown(const ShutdownParameters& /*parameters*/) override {
        valve->closeBeforeShutdown();
    }

private:
    std::shared_ptr<Valve> valve;
    std::shared_ptr<FlowMeter> flowMeter;
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

inline PeripheralFactory makeFactory(const std::map<std::string, std::shared_ptr<PwmMotorDriver>>& motors, ValveControlStrategyType defaultStrategy) {
    return makePeripheralFactory<FlowControlSettings, FlowControlConfig>(
        "flow-control",
        "flow-control",
        [motors](PeripheralInitParameters& params, const std::shared_ptr<FlowControlSettings>& settings) {
            auto motor = findMotor(motors, settings->valve.get()->motor.get());
            auto strategy = settings->valve.get()->createValveControlStrategy(motor);

            auto valve = std::make_shared<Valve>(
                params.name,
                std::move(strategy),
                params.mqttRoot,
                params.services.telemetryPublisher);

            auto flowMeterConfig = settings->flowMeter.get();
            auto flowMeter = std::make_shared<FlowMeter>(
                params.name,
                params.services.pulseCounterManager,
                flowMeterConfig->pin.get(),
                flowMeterConfig->qFactor.get(),
                flowMeterConfig->measurementFrequency.get());

            params.registerFeature("valve", [valve](JsonObject& telemetry) {
                valve->populateTelemetry(telemetry);
            });
            params.registerFeature("flow", [flowMeter](JsonObject& telemetry) {
                flowMeter->populateTelemetry(telemetry);
            });

            return std::make_shared<FlowControl>(params.name, valve, flowMeter);
        },
        defaultStrategy);
}

}    // namespace farmhub::peripherals::flow_control
