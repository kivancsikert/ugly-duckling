#pragma once

#include <chrono>
#include <list>
#include <memory>
#include <variant>

#include <Arduino.h>

#include <ArduinoJson.h>
#include <ArduinoLog.h>

#include <devices/Peripheral.hpp>
#include <kernel/Component.hpp>
#include <kernel/Concurrent.hpp>
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
    virtual ValveState getDefaultState() const = 0;

    virtual String describe() const = 0;
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

    ValveState getDefaultState() const override {
        return ValveState::CLOSED;
    }

    String describe() const override {
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

    ValveState getDefaultState() const override {
        return ValveState::OPEN;
    }

    String describe() const override {
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

    ValveState getDefaultState() const override {
        return ValveState::NONE;
    }

    String describe() const override {
        return "latching with switch duration " + String((int) switchDuration.count()) + "ms with switch duty " + String(switchDuty * 100) + "%";
    }

private:
    const milliseconds switchDuration;
    const double switchDuty;
};

class ValveComponent : public Component {
public:
    ValveComponent(const String& name, PwmMotorDriver& controller, ValveControlStrategy& strategy, shared_ptr<MqttDriver::MqttRoot> mqttRoot)
        : Component(name, mqttRoot)
        , controller(controller)
        , strategy(strategy) {
        Log.infoln("Creating valve '%s' with strategy %s",
            name.c_str(), strategy.describe().c_str());

        controller.stop();

        // TODO Restore stored state?

        mqttRoot->registerCommand("override", [this](const JsonObject& request, JsonObject& response) {
            ValveState targetState = request["state"].as<ValveState>();
            if (targetState == ValveState::NONE) {
                override(ValveState::NONE, time_point<system_clock>());
            } else {
                seconds duration = request.containsKey("duration")
                    ? request["duration"].as<seconds>()
                    : hours { 1 };
                override(targetState, system_clock::now() + duration);
                response["duration"] = duration;
            }
            response["state"] = state;
        });

        Task::loop(name, 3072, [this, name](Task& task) {
            auto now = system_clock::now();
            if (overrideState != ValveState::NONE && now > overrideUntil) {
                Log.traceln("Valve '%s' override expired", name.c_str());
                overrideUntil = time_point<system_clock>();
                overrideState = ValveState::NONE;
            }
            ValveStateUpdate update;
            if (overrideState != ValveState::NONE) {
                update = { overrideState, duration_cast<ticks>(overrideUntil - now) };
                Log.traceln("Valve '%s' override state is %d, will change after %d ms",
                    name.c_str(), static_cast<int>(update.state), update.transitionAfter.count());
            } else {
                update = ValveScheduler::getStateUpdate(schedules, now, this->strategy.getDefaultState());
                Log.traceln("Valve '%s' state is %d, will change after %d ms",
                    name.c_str(), static_cast<int>(update.state), update.transitionAfter.count());
            }
            setState(update.state);
            updateQueue.pollIn(update.transitionAfter, [this](const std::variant<OverrideSpec, ScheduleSpec>& change) {
                std::visit([this](auto&& arg) {
                    using T = std::decay_t<decltype(arg)>;
                    if constexpr (std::is_same_v<T, OverrideSpec>) {
                        overrideState = arg.state;
                        overrideUntil = arg.until;
                    } else if constexpr (std::is_same_v<T, ScheduleSpec>) {
                        schedules = std::list(arg.schedules);
                    }
                },
                    change);
            });
        });
    }

    void setSchedules(const std::list<ValveSchedule>& schedules) {
        Log.traceln("Setting %d schedules for valve %s",
            schedules.size(), name.c_str());
        updateQueue.put(ScheduleSpec { schedules });
    }

    void populateTelemetry(JsonObject& telemetry) {
        telemetry["state"] = this->state;
    }

private:
    void override(ValveState state, time_point<system_clock> until) {
        updateQueue.put(OverrideSpec { state, until });
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

    PwmMotorDriver& controller;
    ValveControlStrategy& strategy;

    ValveState state = ValveState::NONE;

    struct OverrideSpec {
    public:
        ValveState state;
        time_point<system_clock> until;
    };

    struct ScheduleSpec {
    public:
        std::list<ValveSchedule> schedules;
    };

    std::list<ValveSchedule> schedules;
    ValveState overrideState = ValveState::NONE;
    time_point<system_clock> overrideUntil;
    Queue<std::variant<OverrideSpec, ScheduleSpec>> updateQueue { "eventQueue", 1 };
};

}}}    // namespace farmhub::peripherals::valve
