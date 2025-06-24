#pragma once

#include <memory>

#include <Configuration.hpp>
#include <mqtt/MqttDriver.hpp>
#include <peripherals/Motorized.hpp>
#include <peripherals/Peripheral.hpp>
#include <peripherals/flow_meter/FlowMeter.hpp>
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

class FlowControlFactory;

class FlowControl : public Peripheral<FlowControlConfig> {
public:
    FlowControl(
        const std::string& name,
        const std::shared_ptr<MqttRoot>& mqttRoot,
        const std::shared_ptr<PulseCounterManager>& pulseCounterManager,
        const std::shared_ptr<TelemetryPublisher>& telemetryPublisher,
        std::unique_ptr<ValveControlStrategy> strategy,
        const InternalPinPtr& pin,
        double qFactor,
        milliseconds measurementFrequency)
        : Peripheral<FlowControlConfig>(name)
        , valve(name, std::move(strategy), mqttRoot, telemetryPublisher)
        , flowMeter(name, pulseCounterManager, pin, qFactor, measurementFrequency) {
    }

    void configure(const std::shared_ptr<FlowControlConfig> config) override {
        valve.setSchedules(config->schedule.get());
    }

    void shutdown(const ShutdownParameters /*parameters*/) override {
        valve.closeBeforeShutdown();
    }

private:
    ValveComponent valve;
    FlowMeter flowMeter;

    friend class FlowControlFactory;
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

    std::shared_ptr<Peripheral<FlowControlConfig>> createPeripheral(const std::string& name, const std::shared_ptr<FlowControlDeviceConfig> deviceConfig, std::shared_ptr<MqttRoot> mqttRoot, const PeripheralServices& services) override {
        auto strategy = deviceConfig->valve.get()->createValveControlStrategy(this);

        auto flowMeterConfig = deviceConfig->flowMeter.get();
        auto peripheral = std::make_shared<FlowControl>(
            name,
            mqttRoot,
            services.pulseCounterManager,
            services.telemetryPublisher,

            std::move(strategy),

            flowMeterConfig->pin.get(),
            flowMeterConfig->qFactor.get(),
            flowMeterConfig->measurementFrequency.get());

        services.telemetryCollector->registerProvider("flow", name, [peripheral](JsonObject& telemetry) {
            peripheral->flowMeter.populateTelemetry(telemetry);
        });
        services.telemetryCollector->registerProvider("valve", name, [peripheral](JsonObject& telemetry) {
            peripheral->valve.populateTelemetry(telemetry);
        });

        return peripheral;
    }
};

}    // namespace farmhub::peripherals::flow_control
