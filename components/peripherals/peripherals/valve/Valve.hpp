#pragma once

#include <atomic>
#include <chrono>
#include <list>
#include <memory>
#include <utility>
#include <variant>

#include <ArduinoJson.h>

#include <Concurrent.hpp>
#include <NvsStore.hpp>
#include <Task.hpp>
#include <Telemetry.hpp>
#include <Time.hpp>
#include <drivers/MotorDriver.hpp>

#include <peripherals/Peripheral.hpp>
#include <peripherals/valve/ValveScheduler.hpp>

using namespace std::chrono;
using namespace farmhub::kernel::drivers;
using namespace farmhub::peripherals;

namespace farmhub::peripherals::valve {

class ValveControlStrategy {
public:
    virtual ~ValveControlStrategy() = default;

    virtual void open() = 0;
    virtual void close() = 0;
    virtual ValveState getDefaultState() const = 0;

    virtual std::string describe() const = 0;
};

class MotorValveControlStrategy
    : public ValveControlStrategy {
public:
    explicit MotorValveControlStrategy(const std::shared_ptr<PwmMotorDriver>& controller)
        : controller(controller) {
    }

protected:
    const std::shared_ptr<PwmMotorDriver> controller;
};

class HoldingMotorValveControlStrategy
    : public MotorValveControlStrategy {

public:
    HoldingMotorValveControlStrategy(const std::shared_ptr<PwmMotorDriver>& controller, milliseconds switchDuration, double holdDuty)
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
        controller->drive(phase, 1.0);
        Task::delay(switchDuration);
        controller->drive(phase, holdDuty);
    }
};

class NormallyClosedMotorValveControlStrategy
    : public HoldingMotorValveControlStrategy {
public:
    NormallyClosedMotorValveControlStrategy(const std::shared_ptr<PwmMotorDriver>& controller, milliseconds switchDuration, double holdDuty)
        : HoldingMotorValveControlStrategy(controller, switchDuration, holdDuty) {
    }

    void open() override {
        driveAndHold(ValveState::OPEN);
    }

    void close() override {
        controller->stop();
    }

    ValveState getDefaultState() const override {
        return ValveState::CLOSED;
    }

    std::string describe() const override {
        return "normally closed with switch duration " + std::to_string(switchDuration.count()) + " ms and hold duty " + std::to_string(holdDuty * 100) + "%";
    }
};

class NormallyOpenMotorValveControlStrategy
    : public HoldingMotorValveControlStrategy {
public:
    NormallyOpenMotorValveControlStrategy(const std::shared_ptr<PwmMotorDriver>& controller, milliseconds switchDuration, double holdDuty)
        : HoldingMotorValveControlStrategy(controller, switchDuration, holdDuty) {
    }

    void open() override {
        controller->stop();
    }

    void close() override {
        driveAndHold(ValveState::CLOSED);
    }

    ValveState getDefaultState() const override {
        return ValveState::OPEN;
    }

    std::string describe() const override {
        return "normally open with switch duration " + std::to_string(switchDuration.count()) + " ms and hold duty " + std::to_string(holdDuty * 100) + "%";
    }
};

class LatchingMotorValveControlStrategy
    : public MotorValveControlStrategy {
public:
    LatchingMotorValveControlStrategy(const std::shared_ptr<PwmMotorDriver>& controller, milliseconds switchDuration, double switchDuty = 1.0)
        : MotorValveControlStrategy(controller)
        , switchDuration(switchDuration)
        , switchDuty(switchDuty) {
    }

    void open() override {
        controller->drive(MotorPhase::FORWARD, switchDuty);
        Task::delay(switchDuration);
        controller->stop();
    }

    void close() override {
        controller->drive(MotorPhase::REVERSE, switchDuty);
        Task::delay(switchDuration);
        controller->stop();
    }

    ValveState getDefaultState() const override {
        return ValveState::NONE;
    }

    std::string describe() const override {
        return "latching with switch duration " + std::to_string(switchDuration.count()) + " ms and switch duty " + std::to_string(switchDuty * 100) + "%";
    }

private:
    const milliseconds switchDuration;
    const double switchDuty;
};

class LatchingPinValveControlStrategy
    : public ValveControlStrategy {
public:
    explicit LatchingPinValveControlStrategy(const PinPtr& pin)
        : pin(pin) {
        pin->pinMode(Pin::Mode::Output);
    }

    void open() override {
        pin->digitalWrite(1);
    }

    void close() override {
        pin->digitalWrite(0);
    }

    ValveState getDefaultState() const override {
        return ValveState::NONE;
    }

    std::string describe() const override {
        return "latching with pin " + pin->getName();
    }

private:
    PinPtr pin;
};

class Valve
    : Named {
public:
    Valve(
        const std::string& name,
        std::unique_ptr<ValveControlStrategy> _strategy,
        const std::shared_ptr<MqttRoot>& mqttRoot,
        const std::shared_ptr<TelemetryPublisher>& telemetryPublisher)
        : Named(name)
        , nvs(name)
        , strategy(std::move(_strategy))
        , mqttRoot(mqttRoot)
        , telemetryPublisher(telemetryPublisher) {

        LOGI("Creating valve '%s' with strategy %s",
            name.c_str(), strategy->describe().c_str());

        ValveState initState;
        switch (strategy->getDefaultState()) {
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
            auto targetState = request["state"].as<ValveState>();
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

        Task::run(name, 4096, [this, name](Task& /*task*/) {
            auto shouldPublishTelemetry = true;
            while (true) {
                auto now = system_clock::now();
                if (overrideState != ValveState::NONE && now >= overrideUntil.load()) {
                    LOGI("Valve '%s' override expired", name.c_str());
                    overrideUntil = time_point<system_clock>();
                    overrideState = ValveState::NONE;
                    shouldPublishTelemetry = true;
                }

                ValveStateUpdate update {};
                if (overrideState != ValveState::NONE) {
                    update = { overrideState, overrideUntil.load() - now };
                } else {
                    update = ValveScheduler::getStateUpdate(schedules, now, this->strategy->getDefaultState());
                    // If there are no schedules nor default state for the valve, close it
                    if (update.state == ValveState::NONE) {
                        update.state = ValveState::CLOSED;
                    }
                }
                LOGI("Valve '%s' state is %d, will change after %lld ms at %lld",
                    name.c_str(),
                    static_cast<int>(update.state),
                    duration_cast<milliseconds>(update.validFor).count(),
                    duration_cast<seconds>((now + update.validFor).time_since_epoch()).count());
                shouldPublishTelemetry |= transitionTo(update.state);

                if (shouldPublishTelemetry) {
                    this->telemetryPublisher->requestTelemetryPublishing();
                    shouldPublishTelemetry = false;
                }

                // Avoid overflow
                auto validFor = update.validFor < ticks::max()
                    ? duration_cast<ticks>(update.validFor)
                    : ticks::max();
                // TODO Account for time spent in transitionTo()
                updateQueue.pollIn(validFor, [this, &shouldPublishTelemetry](const std::variant<OverrideSpec, ConfigureSpec>& change) {
                    std::visit(
                        [this](auto&& arg) {
                            using T = std::decay_t<decltype(arg)>;
                            if constexpr (std::is_same_v<T, OverrideSpec>) {
                                overrideState = arg.state;
                                overrideUntil = arg.until;
                            } else if constexpr (std::is_same_v<T, ConfigureSpec>) {
                                schedules = std::list(arg.schedules);
                                overrideState = arg.overrideState;
                                overrideUntil = arg.overrideUntil;
                            }
                        },
                        change);
                    shouldPublishTelemetry = true;
                });
            }
        });
    }

    void configure(const std::list<ValveSchedule>& schedules, ValveState overrideState, time_point<system_clock> overrideUntil) {
        LOGD("Configuring valve %s with %d schedules; override state %d until %lld",
            name.c_str(),
            schedules.size(),
            static_cast<int>(overrideState),
            duration_cast<seconds>(overrideUntil.time_since_epoch()).count());
        updateQueue.put(ConfigureSpec { schedules, overrideState, overrideUntil });
    }

    void populateTelemetry(JsonObject& telemetry) {
        telemetry["state"] = this->state;
        auto overrideState = this->overrideState.load();
        if (overrideState != ValveState::NONE) {
            telemetry["overrideState"] = overrideState;
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
            PowerManagementLockGuard sleepLock(PowerManager::noLightSleep);
            strategy->open();
        }
        setState(ValveState::OPEN);
    }

    void close() {
        LOGI("Closing valve '%s'", name.c_str());
        {
            PowerManagementLockGuard sleepLock(PowerManager::noLightSleep);
            strategy->close();
        }
        setState(ValveState::CLOSED);
    }

    bool transitionTo(ValveState state) {
        // Ignore if the state is already set
        if (this->state == state) {
            return false;
        }
        doTransitionTo(state);

        mqttRoot->publish("events/state", [=](JsonObject& json) { json["state"] = state; }, Retention::NoRetain, QoS::AtLeastOnce);
        return true;
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

    NvsStore nvs;
    const std::unique_ptr<ValveControlStrategy> strategy;
    const std::shared_ptr<MqttRoot> mqttRoot;
    const std::shared_ptr<TelemetryPublisher> telemetryPublisher;

    ValveState state = ValveState::NONE;

    struct OverrideSpec {
    public:
        ValveState state;
        time_point<system_clock> until;
    };

    struct ConfigureSpec {
    public:
        std::list<ValveSchedule> schedules;
        ValveState overrideState;
        time_point<system_clock> overrideUntil;
    };

    std::list<ValveSchedule> schedules;
    std::atomic<ValveState> overrideState = ValveState::NONE;
    std::atomic<time_point<system_clock>> overrideUntil;
    Queue<std::variant<OverrideSpec, ConfigureSpec>> updateQueue { "eventQueue", 1 };
};

}    // namespace farmhub::peripherals::valve
