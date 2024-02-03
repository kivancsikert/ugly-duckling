#pragma once

#include <memory>

#include <devices/Peripheral.hpp>
#include <kernel/Configuration.hpp>
#include <kernel/drivers/MqttDriver.hpp>
#include <peripherals/flow_meter/FlowMeterComponent.hpp>
#include <peripherals/flow_meter/FlowMeterConfig.hpp>
#include <peripherals/valve/Valve.hpp>
#include <peripherals/valve/ValveComponent.hpp>
#include <peripherals/valve/ValveConfig.hpp>

using namespace farmhub::devices;
using namespace farmhub::kernel::drivers;
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
    FlowControl(const String& name, shared_ptr<MqttDriver::MqttRoot> mqttRoot,
        PwmMotorDriver& controller, ValveControlStrategy& strategy,
        gpio_num_t pin, double qFactor, milliseconds measurementFrequency)
        : Peripheral<FlowControlConfig>(name, mqttRoot)
        , valve(name, controller, strategy, mqttRoot, [this]() {
            publishTelemetry();
        })
        , flowMeter(name, mqttRoot, pin, qFactor, measurementFrequency) {
    }

    void configure(const FlowControlConfig& config) override {
        valve.setSchedules(config.schedule.get());
    }

    void populateTelemetry(JsonObject& telemetryJson) override {
        valve.populateTelemetry(telemetryJson);
        flowMeter.populateTelemetry(telemetryJson);
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
    FlowControlFactory(const std::list<ServiceRef<PwmMotorDriver>>& motors, ValveControlStrategyType defaultStrategy)
        : PeripheralFactory<FlowControlDeviceConfig, FlowControlConfig, ValveControlStrategyType>("flow-control", defaultStrategy)
        , motors(motors) {
    }

    unique_ptr<Peripheral<FlowControlConfig>> createPeripheral(const String& name, const FlowControlDeviceConfig& deviceConfig, shared_ptr<MqttDriver::MqttRoot> mqttRoot) override {
        const ValveDeviceConfig& valveConfig = deviceConfig.valve.get();
        const FlowMeterDeviceConfig& flowMeterConfig = deviceConfig.flowMeter.get();

        PwmMotorDriver& targetMotor = ValveFactory::findMotor(name, valveConfig.motor.get(), motors);
        ValveControlStrategy* strategy;
        try {
            strategy = createValveControlStrategy(
                valveConfig.strategy.get(),
                valveConfig.switchDuration.get(),
                valveConfig.duty.get() / 100.0);
        } catch (const std::exception& e) {
            throw PeripheralCreationException(name, "failed to create strategy: " + String(e.what()));
        }
        return make_unique<FlowControl>(
            name,
            mqttRoot,

            targetMotor,
            *strategy,

            flowMeterConfig.pin.get(),
            flowMeterConfig.qFactor.get(),
            flowMeterConfig.measurementFrequency.get());
    }

private:
    const std::list<ServiceRef<PwmMotorDriver>> motors;
};

}    // namespace farmhub::peripherals::flow_control
