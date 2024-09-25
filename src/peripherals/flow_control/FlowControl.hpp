#pragma once

#include <memory>

#include <kernel/Configuration.hpp>
#include <kernel/PcntManager.hpp>
#include <kernel/SleepManager.hpp>
#include <kernel/drivers/MqttDriver.hpp>
#include <peripherals/Peripheral.hpp>
#include <peripherals/flow_meter/FlowMeterComponent.hpp>
#include <peripherals/flow_meter/FlowMeterConfig.hpp>
#include <peripherals/valve/Valve.hpp>
#include <peripherals/valve/ValveComponent.hpp>
#include <peripherals/valve/ValveConfig.hpp>

using namespace farmhub::kernel::drivers;
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
        shared_ptr<MqttDriver::MqttRoot> mqttRoot,
        PcntManager& pcnt,
        SleepManager& sleepManager,
        PwmMotorDriver& controller,
        ValveControlStrategy& strategy,
        gpio_num_t pin,
        double qFactor,
        milliseconds measurementFrequency)
        : Peripheral<FlowControlConfig>(name, mqttRoot)
        , valve(name, sleepManager, controller, strategy, mqttRoot, [this]() {
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
    : public PeripheralFactory<FlowControlDeviceConfig, FlowControlConfig, ValveControlStrategyType> {
public:
    FlowControlFactory(
        const ServiceContainer<CurrentSensingMotorDriver>& motors,
        ValveControlStrategyType defaultStrategy)
        : PeripheralFactory<FlowControlDeviceConfig, FlowControlConfig, ValveControlStrategyType>("flow-control", defaultStrategy)
        , motors(motors) {
    }

    unique_ptr<Peripheral<FlowControlConfig>> createPeripheral(const String& name, const FlowControlDeviceConfig& deviceConfig, shared_ptr<MqttDriver::MqttRoot> mqttRoot, PeripheralServices& services) override {
        const ValveDeviceConfig& valveConfig = deviceConfig.valve.get();
        const FlowMeterDeviceConfig& flowMeterConfig = deviceConfig.flowMeter.get();

        PwmMotorDriver& targetMotor = motors.findService(valveConfig.motor.get());
        ValveControlStrategy* strategy;
        try {
            strategy = createValveControlStrategy(
                valveConfig.strategy.get(),
                valveConfig.switchDuration.get(),
                valveConfig.holdDuty.get() / 100.0);
        } catch (const std::exception& e) {
            throw PeripheralCreationException("failed to create strategy: " + String(e.what()));
        }
        return make_unique<FlowControl>(
            name,
            mqttRoot,

            services.pcntManager,
            services.sleepManager,
            targetMotor,
            *strategy,

            flowMeterConfig.pin.get(),
            flowMeterConfig.qFactor.get(),
            flowMeterConfig.measurementFrequency.get());
    }

private:
    const ServiceContainer<CurrentSensingMotorDriver>& motors;
};

}    // namespace farmhub::peripherals::flow_control
