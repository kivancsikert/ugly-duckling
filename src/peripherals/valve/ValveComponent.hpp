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

#include <peripherals/valve/ValveScheduler.hpp>

using namespace std::chrono;
using std::make_unique;
using std::move;
using std::unique_ptr;

using namespace farmhub::devices;
using namespace farmhub::kernel::drivers;

namespace farmhub { namespace peripherals { namespace valve {

class ValveControlStrategy {
public:
    virtual void open(PwmMotorDriver& controller) = 0;
    virtual void close(PwmMotorDriver& controller) = 0;
    virtual ValveState getDefaultState() = 0;

    virtual String describe() = 0;
};

class HoldingValveControlStrategy
    : public ValveControlStrategy {

public:
    HoldingValveControlStrategy(milliseconds switchDuration, double holdDuty)
        : switchDuration(switchDuration)
        , holdDuty(holdDuty) {
    }

protected:
    void driveAndHold(PwmMotorDriver& controller, ValveState targetState) {
        switch (targetState) {
            case ValveState::OPEN:
                controller.drive(MotorPhase::FORWARD, holdDuty);
                break;
            case ValveState::CLOSED:
                controller.drive(MotorPhase::REVERSE, holdDuty);
                break;
            default:
                // Ignore
                break;
        }
        delay(switchDuration.count());
        controller.stop();
    }

    const milliseconds switchDuration;
    const double holdDuty;
};

class NormallyClosedValveControlStrategy
    : public HoldingValveControlStrategy {
public:
    NormallyClosedValveControlStrategy(milliseconds switchDuration, double holdDuty)
        : HoldingValveControlStrategy(switchDuration, holdDuty) {
    }

    void open(PwmMotorDriver& controller) override {
        driveAndHold(controller, ValveState::OPEN);
    }

    void close(PwmMotorDriver& controller) override {
        controller.stop();
    }

    ValveState getDefaultState() override {
        return ValveState::CLOSED;
    }

    String describe() override {
        return "normally closed with switch duration " + String((int) switchDuration.count()) + "ms and hold duty " + String(holdDuty * 100) + "%";
    }
};

class NormallyOpenValveControlStrategy
    : public HoldingValveControlStrategy {
public:
    NormallyOpenValveControlStrategy(milliseconds switchDuration, double holdDuty)
        : HoldingValveControlStrategy(switchDuration, holdDuty) {
    }

    void open(PwmMotorDriver& controller) override {
        controller.stop();
    }

    void close(PwmMotorDriver& controller) override {
        driveAndHold(controller, ValveState::CLOSED);
    }

    ValveState getDefaultState() override {
        return ValveState::OPEN;
    }

    String describe() override {
        return "normally open with switch duration " + String((int) switchDuration.count()) + "ms and hold duty " + String(holdDuty * 100) + "%";
    }
};

class LatchingValveControlStrategy
    : public ValveControlStrategy {
public:
    LatchingValveControlStrategy(milliseconds switchDuration, double switchDuty = 1.0)
        : switchDuration(switchDuration)
        , switchDuty(switchDuty) {
    }

    void open(PwmMotorDriver& controller) override {
        controller.drive(MotorPhase::FORWARD, switchDuty);
        delay(switchDuration.count());
        controller.stop();
    }

    void close(PwmMotorDriver& controller) override {
        controller.drive(MotorPhase::REVERSE, switchDuty);
        delay(switchDuration.count());
        controller.stop();
    }

    ValveState getDefaultState() override {
        return ValveState::NONE;
    }

    String describe() override {
        return "latching with switch duration " + String((int) switchDuration.count()) + "ms with switch duty " + String(switchDuty * 100) + "%";
    }

private:
    const milliseconds switchDuration;
    const double switchDuty;
};

class ValveComponent {
public:
    ValveComponent(const String& name, PwmMotorDriver& controller, ValveControlStrategy& strategy, MqttDriver::MqttRoot mqttRoot)
        : name(name)
        , controller(controller)
        , strategy(strategy)
        , mqttRoot(mqttRoot) {
        Log.infoln("Creating valve '%s' with strategy %s",
            name.c_str(), strategy.describe().c_str());

        controller.stop();

        // TODO Restore stored state?

        Task::loop(name, 3072, [this, name](Task& task) {
            auto now = system_clock::now();
            auto update = ValveScheduler::getStateUpdate(schedules, now, this->strategy.getDefaultState());
            Log.traceln("Valve '%s' state is %d, will change after %d ms",
                name.c_str(), static_cast<int>(update.state), update.transitionAfter.count());
            setState(update.state);
            task.delayUntil(update.transitionAfter);
        });
    }

    void setSchedules(std::list<ValveSchedule> schedules) {
        Log.traceln("Setting %d schedules for valve %s",
            schedules.size(), name.c_str());
        // TODO Do this thread safe?
        this->schedules = std::list(schedules);
        // TODO Notify the task to reevaluate the schedule
    }

    void open() {
        Log.traceln("Opening valve %s", name.c_str());
        strategy.open(controller);
        this->state = ValveState::OPEN;
    }

    void close() {
        Log.traceln("Closing valve");
        strategy.close(controller);
        this->state = ValveState::CLOSED;
    }

    void reset() {
        Log.traceln("Resetting valve");
        controller.stop();
    }

    ValveState getState() {
        return state;
    }

    void setState(ValveState state) {
        switch (state) {
            case ValveState::OPEN:
                open();
                break;
            case ValveState::CLOSED:
                close();
                break;
            default:
                // Ignore
                break;
        }
        // TODO Publish event
    }

    void populateTelemetry(JsonObject& telemetry) {
        telemetry["state"] = this->state;
    }

private:
    const String name;
    PwmMotorDriver& controller;
    ValveControlStrategy& strategy;
    MqttDriver::MqttRoot mqttRoot;

    ValveState state = ValveState::NONE;
    TaskHandle* task = nullptr;
    std::list<ValveSchedule> schedules;
};

}}}    // namespace farmhub::peripherals::valve
