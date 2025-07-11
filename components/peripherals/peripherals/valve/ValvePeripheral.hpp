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
        valve->configure(config->override.getAsOptional(), config->schedule.get());
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

    std::shared_ptr<Peripheral<ValveConfig>> createPeripheral(PeripheralInitParameters& params, const std::shared_ptr<ValveSettings>& settings) override {
        auto strategy = settings->createValveControlStrategy(this);
        auto valve = std::make_shared<Valve>(
            params.name,
            std::move(strategy),
            params.mqttRoot,
            params.services.telemetryPublisher);
        params.registerFeature("valve", [valve](JsonObject& telemetry) {
            valve->populateTelemetry(telemetry);
        });
        return std::make_shared<ValvePeripheral>(params.name, valve);
    }
};

}    // namespace farmhub::peripherals::valve
