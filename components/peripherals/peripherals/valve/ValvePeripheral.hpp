#pragma once

#include <chrono>
#include <list>
#include <memory>

#include <ArduinoJson.h>

#include <Task.hpp>
#include <Telemetry.hpp>
#include <drivers/MotorDriver.hpp>

#include <peripherals/Motorized.hpp>
#include <peripherals/Peripheral.hpp>
#include <peripherals/valve/Valve.hpp>
#include <peripherals/valve/ValveConfig.hpp>
#include <peripherals/valve/ValveScheduler.hpp>

using namespace std::chrono;
using namespace farmhub::kernel::drivers;
using namespace farmhub::peripherals;

namespace farmhub::peripherals::valve {

class ValvePeripheral
    : public Peripheral<ValveConfig> {
public:
    ValvePeripheral(
        const std::string& name,
        const std::shared_ptr<Valve>& valve)
        : Peripheral<ValveConfig>(name)
        , valve(valve) {
    }

    void configure(const std::shared_ptr<ValveConfig> config) override {
        valve->configure(config->schedule.get(), config->overrideState.get(), config->overrideUntil.get());
    }

    void shutdown(const ShutdownParameters /*parameters*/) override {
        valve->closeBeforeShutdown();
    }

private:
    std::shared_ptr<Valve> valve;
};

class ValveFactory
    : public PeripheralFactory<ValveSettings, ValveConfig, ValveControlStrategyType>,
      protected Motorized {
public:
    ValveFactory(
        const std::map<std::string, std::shared_ptr<PwmMotorDriver>>& motors,
        ValveControlStrategyType defaultStrategy)
        : PeripheralFactory<ValveSettings, ValveConfig, ValveControlStrategyType>("valve", defaultStrategy)
        , Motorized(motors) {
    }

    std::shared_ptr<Peripheral<ValveConfig>> createPeripheral(const std::string& name, const std::shared_ptr<ValveSettings>& settings, const std::shared_ptr<MqttRoot>& mqttRoot, const PeripheralServices& services) override {
        auto strategy = settings->createValveControlStrategy(this);
        auto valve = std::make_shared<Valve>(
            name,
            std::move(strategy),
            mqttRoot,
            services.telemetryPublisher);
        services.telemetryCollector->registerFeature("valve", name, [valve](JsonObject& telemetry) {
            valve->populateTelemetry(telemetry);
        });
        return std::make_shared<ValvePeripheral>(name, valve);
    }
};

}    // namespace farmhub::peripherals::valve
