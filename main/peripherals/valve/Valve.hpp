#pragma once

#include <chrono>
#include <list>
#include <memory>

#include <Arduino.h>

#include <ArduinoJson.h>

#include <kernel/Service.hpp>
#include <kernel/SleepManager.hpp>
#include <kernel/Task.hpp>
#include <kernel/Telemetry.hpp>
#include <kernel/drivers/MotorDriver.hpp>

#include <peripherals/Motorized.hpp>
#include <peripherals/Peripheral.hpp>
#include <peripherals/valve/ValveComponent.hpp>
#include <peripherals/valve/ValveConfig.hpp>
#include <peripherals/valve/ValveScheduler.hpp>

using namespace std::chrono;
using std::make_unique;
using std::move;
using std::unique_ptr;

using namespace farmhub::kernel::drivers;
using namespace farmhub::peripherals;

namespace farmhub::peripherals::valve {

class Valve
    : public Peripheral<ValveConfig> {
public:
    Valve(
        const String& name,
        SleepManager& sleepManager,
        PwmMotorDriver& controller,
        ValveControlStrategy& strategy,
        shared_ptr<MqttDriver::MqttRoot> mqttRoot)
        : Peripheral<ValveConfig>(name, mqttRoot)
        , valve(name, sleepManager, controller, strategy, mqttRoot, [this]() {
            publishTelemetry();
        }) {
    }

    void configure(const ValveConfig& config) override {
        valve.setSchedules(config.schedule.get());
    }

    void populateTelemetry(JsonObject& telemetry) override {
        valve.populateTelemetry(telemetry);
    }

    void shutdown(const ShutdownParameters parameters) override {
        valve.closeBeforeShutdown();
    }

private:
    ValveComponent valve;
};

class ValveFactory
    : public PeripheralFactory<ValveDeviceConfig, ValveConfig, ValveControlStrategyType>,
      protected Motorized {
public:
    ValveFactory(
        const std::list<ServiceRef<PwmMotorDriver>>& motors,
        ValveControlStrategyType defaultStrategy)
        : PeripheralFactory<ValveDeviceConfig, ValveConfig, ValveControlStrategyType>("valve", defaultStrategy)
        , Motorized(motors) {
    }

    unique_ptr<Peripheral<ValveConfig>> createPeripheral(const String& name, const ValveDeviceConfig& deviceConfig, shared_ptr<MqttDriver::MqttRoot> mqttRoot, PeripheralServices& services) override {
        PwmMotorDriver& targetMotor = findMotor(deviceConfig.motor.get());
        ValveControlStrategy* strategy;
        try {
            strategy = createValveControlStrategy(
                deviceConfig.strategy.get(),
                deviceConfig.switchDuration.get(),
                deviceConfig.holdDuty.get() / 100.0);
        } catch (const std::exception& e) {
            throw PeripheralCreationException("failed to create strategy: " + String(e.what()));
        }
        return make_unique<Valve>(name, services.sleepManager, targetMotor, *strategy, mqttRoot);
    }
};

}    // namespace farmhub::peripherals::valve
