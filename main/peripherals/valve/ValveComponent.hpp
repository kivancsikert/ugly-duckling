#pragma once

#include <atomic>
#include <chrono>
#include <list>
#include <memory>
#include <variant>

#include <Arduino.h>

#include <ArduinoJson.h>

#include <kernel/Component.hpp>
#include <kernel/Concurrent.hpp>
#include <kernel/Log.hpp>
#include <kernel/NvsStore.hpp>
#include <kernel/Service.hpp>
#include <kernel/Task.hpp>
#include <kernel/Telemetry.hpp>
#include <kernel/Time.hpp>
#include <kernel/drivers/MotorDriver.hpp>

#include <peripherals/Peripheral.hpp>
#include <peripherals/valve/ValveScheduler.hpp>

using namespace std::chrono;
using std::make_unique;
using std::move;
using std::unique_ptr;

using namespace farmhub::kernel::drivers;
using namespace farmhub::peripherals;

namespace farmhub::peripherals::valve {

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
                driveAndHold(controller, MotorPhase::FORWARD);
                break;
            case ValveState::CLOSED:
                driveAndHold(controller, MotorPhase::REVERSE);
                break;
            default:
                // Ignore
                break;
        }
    }

    const milliseconds switchDuration;
    const double holdDuty;

private:
    void driveAndHold(PwmMotorDriver& controller, MotorPhase phase) {
        controller.drive(phase, 1.0);
        delay(switchDuration.count());
        controller.drive(phase, holdDuty);
    }
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
    ValveComponent(
        const String& name,
        SleepManager& sleepManager,
        PwmMotorDriver& controller,
        ValveControlStrategy& strategy,
        shared_ptr<MqttDriver::MqttRoot> mqttRoot,
        std::function<void()> publishTelemetry)
        : Component(name, mqttRoot)
        , sleepManager(sleepManager)
        , controller(controller)
        , nvs(name)
        , strategy(strategy)
        , publishTelemetry(publishTelemetry) {

        Log.info("Creating valve '%s' with strategy %s",
            name.c_str(), strategy.describe().c_str());

        controller.stop();

        // Rewrite this to a switch statement
        if (strategy.getDefaultState() == ValveState::OPEN) {
            state = ValveState::OPEN;
        } else if (strategy.getDefaultState() == ValveState::CLOSED) {
            state = ValveState::CLOSED;
        }

        switch (strategy.getDefaultState()) {
            case ValveState::OPEN:
                Log.info("Assuming valve '%s' is open by default",
                    name.c_str());
                state = ValveState::OPEN;
                break;
            case ValveState::CLOSED:
                Log.info("Assuming valve '%s' is closed by default",
                    name.c_str());
                state = ValveState::CLOSED;
                break;
            default:
                // Try to load from NVS
                ValveState lastStoredState;
                if (nvs.get("state", lastStoredState)) {
                    state = lastStoredState;
                    Log.info("Restored state for valve '%s' from NVS: %d",
                        name.c_str(), static_cast<int>(state));
                } else {
                    Log.info("No stored state for valve '%s'",
                        name.c_str());
                }
                break;
        }

        // TODO Restore stored state?

        mqttRoot->registerCommand("override", [this](const JsonObject& request, JsonObject& response) {
            ValveState targetState = request["state"].as<ValveState>();
            if (targetState == ValveState::NONE) {
                override(ValveState::NONE, time_point<system_clock>());
            } else {
                seconds duration = request["duration"].is<JsonVariant>()
                    ? request["duration"].as<seconds>()
                    : hours { 1 };
                override(targetState, system_clock::now() + duration);
                response["duration"] = duration;
            }
            response["state"] = state;
        });

        Task::loop(name, 4096, [this, name](Task& task) {
            auto now = system_clock::now();
            if (overrideState != ValveState::NONE && now >= overrideUntil.load()) {
                Log.info("Valve '%s' override expired", name.c_str());
                overrideUntil = time_point<system_clock>();
                overrideState = ValveState::NONE;
            }

            ValveStateUpdate update;
            if (overrideState != ValveState::NONE) {
                update = { overrideState, overrideUntil.load() - now };
            } else {
                update = ValveScheduler::getStateUpdate(schedules, now, this->strategy.getDefaultState());
                // If there are no schedules nor default state for the valve, close it
                if (update.state == ValveState::NONE) {
                    update.state = ValveState::CLOSED;
                }
            }
            Log.info("Valve '%s' state is %d, will change after %.2f sec at %lld",
                name.c_str(),
                static_cast<int>(update.state),
                duration_cast<milliseconds>(update.validFor).count() / 1000.0,
                duration_cast<seconds>((now + update.validFor).time_since_epoch()).count());
            transitionTo(update.state);

            // Avoid overflow
            auto validFor = update.validFor < ticks::max()
                ? duration_cast<ticks>(update.validFor)
                : ticks::max();
            // TODO Account for time spent in transitionTo()
            updateQueue.pollIn(validFor, [this](const std::variant<OverrideSpec, ScheduleSpec>& change) {
                std::visit(
                    [this](auto&& arg) {
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
        Log.debug("Setting %d schedules for valve %s",
            schedules.size(), name.c_str());
        updateQueue.put(ScheduleSpec { schedules });
    }

    void populateTelemetry(JsonObject& telemetry) {
        telemetry["state"] = this->state;
        auto overrideUntil = this->overrideUntil.load();
        if (overrideUntil != time_point<system_clock>()) {
            time_t rawtime = system_clock::to_time_t(overrideUntil);
            auto timeinfo = gmtime(&rawtime);
            char buffer[80];
            strftime(buffer, 80, "%FT%TZ", timeinfo);
            telemetry["overrideEnd"] = String(buffer);
            telemetry["overrideState"] = this->overrideState.load();
        }
    }

    void closeBeforeShutdown() {
        // TODO Lock the valve to prevent concurrent access
        Log.info("Shutting down valve '%s', closing it",
            name.c_str());
        close();
    }

private:
    void override(ValveState state, time_point<system_clock> until) {
        if (state == ValveState::NONE) {
            Log.info("Clearing override for valve '%s'", name.c_str());
        } else {
            Log.info("Overriding valve '%s' to state %d until %lld",
                name.c_str(), static_cast<int>(state), duration_cast<seconds>(until.time_since_epoch()).count());
        }
        updateQueue.put(OverrideSpec { state, until });
    }

    void open() {
        Log.info("Opening valve '%s'", name.c_str());
        KeepAwake keepAwake(sleepManager);
        strategy.open(controller);
        setState(ValveState::OPEN);
    }

    void close() {
        Log.info("Closing valve '%s'", name.c_str());
        KeepAwake keepAwake(sleepManager);
        strategy.close(controller);
        setState(ValveState::CLOSED);
    }

    void transitionTo(ValveState state) {
        // Ignore if the state is already set
        if (this->state == state) {
            return;
        }

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
        mqttRoot->publish("events/state", [=](JsonObject& json) {
            json["state"] = state;
        });
        publishTelemetry();
    }

    void setState(ValveState state) {
        this->state = state;
        if (!nvs.set("state", state)) {
            Log.error("Failed to store state for valve '%s': %d",
                name.c_str(), static_cast<int>(state));
        }
    }

    SleepManager& sleepManager;
    PwmMotorDriver& controller;
    NvsStore nvs;
    ValveControlStrategy& strategy;
    std::function<void()> publishTelemetry;

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

    std::list<ValveSchedule> schedules = {};
    std::atomic<ValveState> overrideState = ValveState::NONE;
    std::atomic<time_point<system_clock>> overrideUntil = time_point<system_clock>();
    Queue<std::variant<OverrideSpec, ScheduleSpec>> updateQueue { "eventQueue", 1 };
};

}    // namespace farmhub::peripherals::valve
