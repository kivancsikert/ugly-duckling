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
#include <peripherals/valve/ValveComponent.hpp>
#include <peripherals/valve/ValveConfig.hpp>
#include <peripherals/valve/ValveScheduler.hpp>

using namespace std::chrono;
using namespace farmhub::kernel::drivers;
using namespace farmhub::peripherals;

namespace farmhub::peripherals::valve {

class ValveFactory;

class Valve
    : public Peripheral<ValveConfig> {
public:
    Valve(
        const std::string& name,
        std::unique_ptr<ValveControlStrategy> strategy,
        const std::shared_ptr<MqttRoot>& mqttRoot,
        const std::shared_ptr<TelemetryPublisher>& telemetryPublisher)
        : Peripheral<ValveConfig>(name, mqttRoot)
        , valve(name, std::move(strategy), mqttRoot, telemetryPublisher) {
    }

    void configure(const std::shared_ptr<ValveConfig> config) override {
        valve.setSchedules(config->schedule.get());
    }

    void shutdown(const ShutdownParameters /*parameters*/) override {
        valve.closeBeforeShutdown();
    }

private:
    ValveComponent valve;
    friend class ValveFactory;
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

    std::shared_ptr<Peripheral<ValveConfig>> createPeripheral(const std::string& name, const std::shared_ptr<ValveDeviceConfig> deviceConfig, std::shared_ptr<MqttRoot> mqttRoot, const PeripheralServices& services) override {
        auto strategy = deviceConfig->createValveControlStrategy(this);
        auto peripheral = std::make_shared<Valve>(name, std::move(strategy), mqttRoot, services.telemetryPublisher);
        services.telemetryCollector->registerProvider("valve", name, [peripheral](JsonObject& telemetry) {
            peripheral->valve.populateTelemetry(telemetry);
        });
        return peripheral;
    }
};

}    // namespace farmhub::peripherals::valve
