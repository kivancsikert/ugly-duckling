#pragma once

#include <chrono>
#include <list>
#include <memory>

#include <Arduino.h>

#include <ArduinoJson.h>
#include <ArduinoLog.h>

#include <devices/Peripheral.hpp>
#include <kernel/Service.hpp>
#include <kernel/Task.hpp>
#include <kernel/Telemetry.hpp>
#include <kernel/drivers/MotorDriver.hpp>

#include <peripherals/valve/ValveComponent.hpp>
#include <peripherals/valve/ValveConfig.hpp>
#include <peripherals/valve/ValveScheduler.hpp>

using namespace std::chrono;
using std::make_unique;
using std::move;
using std::unique_ptr;

using namespace farmhub::devices;
using namespace farmhub::kernel::drivers;

namespace farmhub { namespace peripherals { namespace valve {

class Valve
    : public Peripheral<ValveConfig> {
public:
    Valve(const String& name, PwmMotorDriver& controller, ValveControlStrategy& strategy, MqttDriver::MqttRoot mqttRoot)
        : Peripheral<ValveConfig>(name, mqttRoot)
        , valve(name, controller, strategy, mqttRoot) {
    }

    void configure(const ValveConfig& config) override {
        valve.setSchedules(config.schedule.get());
    }

    void populateTelemetry(JsonObject& telemetry) override {
        telemetry["state"] = valve.getState();
    }

private:
    ValveComponent valve;
};

class ValveFactory
    : public PeripheralFactory<ValveDeviceConfig, ValveConfig> {
public:
    ValveFactory(const std::list<ServiceRef<PwmMotorDriver>>& motors, ValveControlStrategyType defaultStrategy)
        : PeripheralFactory<ValveDeviceConfig, ValveConfig>("valve")
        , motors(motors)
        , defaultStrategy(defaultStrategy) {
    }

    ValveDeviceConfig* createDeviceConfig() override {
        return new ValveDeviceConfig(defaultStrategy);
    }

    Valve* createPeripheral(const String& name, const ValveDeviceConfig& deviceConfig, MqttDriver::MqttRoot mqttRoot) override {
        PwmMotorDriver* targetMotor = nullptr;
        for (auto& motor : motors) {
            if (motor.getName() == deviceConfig.motor.get()) {
                targetMotor = &(motor.get());
                break;
            }
        }
        if (targetMotor == nullptr) {
            // TODO Add proper error handling
            Log.errorln("Failed to find motor: %s",
                deviceConfig.motor.get().c_str());
            return nullptr;
        }
        ValveControlStrategy* strategy = createStrategy(deviceConfig);
        if (strategy == nullptr) {
            // TODO Add proper error handling
            Log.errorln("Failed to create strategy");
            return nullptr;
        }
        return new Valve(name, *targetMotor, *strategy, mqttRoot);
    }

private:
    ValveControlStrategy* createStrategy(const ValveDeviceConfig& config) {
        return createValveControlStrategy(
            config.strategy.get(),
            config.switchDuration.get(),
            config.duty.get() / 100.0);
    }

    const std::list<ServiceRef<PwmMotorDriver>> motors;
    const ValveControlStrategyType defaultStrategy;
};

}}}    // namespace farmhub::peripherals::valve
