#pragma once

#include <chrono>
#include <list>
#include <memory>

#include <ArduinoJson.h>

#include <kernel/Task.hpp>
#include <kernel/Telemetry.hpp>
#include <kernel/drivers/MotorDriver.hpp>

#include <peripherals/Motorized.hpp>
#include <peripherals/Peripheral.hpp>
#include <peripherals/valve/ValveComponent.hpp>
#include <peripherals/valve/ValveConfig.hpp>
#include <peripherals/valve/ValveScheduler.hpp>

using namespace std::chrono;
using namespace farmhub::kernel::drivers;
using namespace farmhub::peripherals;

namespace farmhub::peripherals::valve {

class Valve
    : public Peripheral<ValveConfig> {
public:
    Valve(
        const std::string& name,
        ValveControlStrategy& strategy,
        std::shared_ptr<MqttRoot> mqttRoot)
        : Peripheral<ValveConfig>(name, mqttRoot)
        , valve(name, strategy, mqttRoot, [this]() {
            publishTelemetry();
        }) {
    }

    void configure(const std::shared_ptr<ValveConfig> config) override {
        valve.setSchedules(config->schedule.get());
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
        const std::map<std::string, std::shared_ptr<PwmMotorDriver>>& motors,
        ValveControlStrategyType defaultStrategy)
        : PeripheralFactory<ValveDeviceConfig, ValveConfig, ValveControlStrategyType>("valve", defaultStrategy)
        , Motorized(motors) {
    }

    std::unique_ptr<Peripheral<ValveConfig>> createPeripheral(const std::string& name, const std::shared_ptr<ValveDeviceConfig> deviceConfig, std::shared_ptr<MqttRoot> mqttRoot, const PeripheralServices& services) override {
        auto strategy = deviceConfig->createValveControlStrategy(this);
        return std::make_unique<Valve>(name, *strategy, mqttRoot);
    }
};

}    // namespace farmhub::peripherals::valve
