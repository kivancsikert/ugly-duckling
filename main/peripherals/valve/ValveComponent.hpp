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
    virtual void open() = 0;
    virtual void close() = 0;
    virtual ValveState getDefaultState() const = 0;

    virtual String describe() const = 0;
};

class MotorValveControlStrategy
    : public ValveControlStrategy {
public:
    MotorValveControlStrategy(PwmMotorDriver& controller)
        : controller(controller) {
    }

protected:
    PwmMotorDriver& controller;
};

class HoldingMotorValveControlStrategy
    : public MotorValveControlStrategy {

public:
    HoldingMotorValveControlStrategy(PwmMotorDriver& controller, milliseconds switchDuration, double holdDuty)
        : MotorValveControlStrategy(controller)
        , switchDuration(switchDuration)
        , holdDuty(holdDuty) {
    }

protected:
    void driveAndHold(ValveState targetState) {
        switch (targetState) {
            case ValveState::OPEN:
                driveAndHold(MotorPhase::FORWARD);
                break;
            case ValveState::CLOSED:
                driveAndHold(MotorPhase::REVERSE);
                break;
            default:
                // Ignore
                break;
        }
    }

    const milliseconds switchDuration;
    const double holdDuty;

private:
    void driveAndHold(MotorPhase phase) {
        controller.drive(phase, 1.0);
        delay(switchDuration.count());
        controller.drive(phase, holdDuty);
    }
};

class NormallyClosedMotorValveControlStrategy
    : public HoldingMotorValveControlStrategy {
public:
    NormallyClosedMotorValveControlStrategy(PwmMotorDriver& controller, milliseconds switchDuration, double holdDuty)
        : HoldingMotorValveControlStrategy(controller, switchDuration, holdDuty) {
    }

    void open() override {
        driveAndHold(ValveState::OPEN);
    }

    void close() override {
        controller.stop();
    }

    ValveState getDefaultState() const override {
        return ValveState::CLOSED;
    }

    String describe() const override {
        return "normally closed with switch duration " + String((int) switchDuration.count()) + "ms and hold duty " + String(holdDuty * 100) + "%";
    }
};

class NormallyOpenMotorValveControlStrategy
    : public HoldingMotorValveControlStrategy {
public:
    NormallyOpenMotorValveControlStrategy(PwmMotorDriver& controller, milliseconds switchDuration, double holdDuty)
        : HoldingMotorValveControlStrategy(controller, switchDuration, holdDuty) {
    }

    void open() override {
        controller.stop();
    }

    void close() override {
        driveAndHold(ValveState::CLOSED);
    }

    ValveState getDefaultState() const override {
        return ValveState::OPEN;
    }

    String describe() const override {
        return "normally open with switch duration " + String((int) switchDuration.count()) + "ms and hold duty " + String(holdDuty * 100) + "%";
    }
};

class LatchingMotorValveControlStrategy
    : public MotorValveControlStrategy {
public:
    LatchingMotorValveControlStrategy(PwmMotorDriver& controller, milliseconds switchDuration, double switchDuty = 1.0)
        : MotorValveControlStrategy(controller)
        , switchDuration(switchDuration)
        , switchDuty(switchDuty) {
    }

    void open() override {
        controller.drive(MotorPhase::FORWARD, switchDuty);
        delay(switchDuration.count());
        controller.stop();
    }

    void close() override {
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

class LatchingPinValveControlStrategy
    : public ValveControlStrategy {
public:
    LatchingPinValveControlStrategy(PinPtr pin)
        : pin(pin) {
        pin->pinMode(OUTPUT);
    }

    void open() override {
        pin->digitalWrite(HIGH);
    }

    void close() override {
        pin->digitalWrite(LOW);
    }

    ValveState getDefaultState() const override {
        return ValveState::NONE;
    }

    String describe() const override {
        return "latching with pin " + pin->getName();
    }

private:
    PinPtr pin;
};

class ValveComponent : public Component {
public:
    ValveComponent(
        const String& name,
        ValveControlStrategy& strategy,
        shared_ptr<MqttRoot> mqttRoot,
        std::function<void()> publishTelemetry)
        : Component(name, mqttRoot)
        , nvs(name)
        , strategy(strategy)
        , publishTelemetry(publishTelemetry) {

        LOGI("Creating valve '%s' with strategy %s",
            name.c_str(), strategy.describe().c_str());

        ValveState initState;
        switch (strategy.getDefaultState()) {
            case ValveState::OPEN:
                LOGI("Assuming valve '%s' is open by default",
                    name.c_str());
                initState = ValveState::OPEN;
                break;
            case ValveState::CLOSED:
                LOGI("Assuming valve '%s' is closed by default",
                    name.c_str());
                initState = ValveState::CLOSED;
                break;
            default:
                // Try to load from NVS
                ValveState lastStoredState;
                if (nvs.get("state", lastStoredState)) {
                    initState = lastStoredState;
                    LOGI("Restored state for valve '%s' from NVS: %d",
                        name.c_str(), static_cast<int>(state));
                } else {
                    initState = ValveState::CLOSED;
                    LOGI("No stored state for valve '%s', defaulting to closed",
                        name.c_str());
                }
                break;
        }
        doTransitionTo(initState);

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
                LOGI("Valve '%s' override expired", name.c_str());
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
            LOGI("Valve '%s' state is %d, will change after %.2f sec at %lld",
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
        LOGD("Setting %d schedules for valve %s",
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
        LOGI("Shutting down valve '%s', closing it",
            name.c_str());
        close();
    }

private:
    void override(ValveState state, time_point<system_clock> until) {
        if (state == ValveState::NONE) {
            LOGI("Clearing override for valve '%s'", name.c_str());
        } else {
            LOGI("Overriding valve '%s' to state %d until %lld",
                name.c_str(), static_cast<int>(state), duration_cast<seconds>(until.time_since_epoch()).count());
        }
        updateQueue.put(OverrideSpec { state, until });
    }

    void open() {
        LOGI("Opening valve '%s'", name.c_str());
        {
            PowerManagementLockGuard noSleep(preventLightSleep);
            strategy.open();
        }
        setState(ValveState::OPEN);
    }

    void close() {
        LOGI("Closing valve '%s'", name.c_str());
        {
            PowerManagementLockGuard noSleep(preventLightSleep);
            strategy.close();
        }
        setState(ValveState::CLOSED);
    }

    void transitionTo(ValveState state) {
        // Ignore if the state is already set
        if (this->state == state) {
            return;
        }
        doTransitionTo(state);

        mqttRoot->publish("events/state", [=](JsonObject& json) {
            json["state"] = state;
        }, Retention::NoRetain, QoS::AtLeastOnce);
        publishTelemetry();
    }

    void doTransitionTo(ValveState state) {
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
    }

    void setState(ValveState state) {
        this->state = state;
        if (!nvs.set("state", state)) {
            LOGE("Failed to store state for valve '%s': %d",
                name.c_str(), static_cast<int>(state));
        }
    }

    PowerManagementLock preventLightSleep { name, ESP_PM_NO_LIGHT_SLEEP };

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
