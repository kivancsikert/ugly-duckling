#pragma once

#include <atomic>
#include <chrono>
#include <list>
#include <memory>
#include <utility>
#include <variant>

#include <ArduinoJson.h>

#include <Concurrent.hpp>
#include <Named.hpp>
#include <NvsStore.hpp>
#include <Task.hpp>
#include <Telemetry.hpp>
#include <Time.hpp>
#include <drivers/MotorDriver.hpp>

#include <peripherals/Peripheral.hpp>
#include <peripherals/valve/ValveConfig.hpp>
#include <peripherals/valve/ValveControlStrategy.hpp>
#include <peripherals/valve/ValveScheduler.hpp>

using namespace std::chrono;
using namespace farmhub::kernel::drivers;
using namespace farmhub::peripherals;

namespace farmhub::peripherals::valve {

class Valve
    : public Named
    , public HasConfig<ValveConfig>
    , public HasShutdown {
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
                    update = {
                        .state = overrideState,
                        .validFor = overrideUntil.load() - now,
                    };
                } else {
                    update = ValveScheduler::getStateUpdate(schedules, now);
                    // If there are no schedules, set it to default
                    if (update.state == ValveState::NONE) {
                        update.state = this->strategy->getDefaultState();
                        // If the default state is not set for the valve, close it
                        if (update.state == ValveState::NONE) {
                            update.state = ValveState::CLOSED;
                        }
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

    void configure(const std::shared_ptr<ValveConfig>& config) override {
        configure(config->schedule.get(), config->overrideState.get(), config->overrideUntil.get());
    }

    void configure(const std::list<ValveSchedule>& schedules, ValveState overrideState, time_point<system_clock> overrideUntil) {
        LOGD("Configuring valve '%s' with %d schedules; override state %d until %lld",
            name.c_str(),
            schedules.size(),
            static_cast<int>(overrideState),
            duration_cast<seconds>(overrideUntil.time_since_epoch()).count());
        updateQueue.put(ConfigureSpec {
            .schedules = schedules,
            .overrideState = overrideState,
            .overrideUntil = overrideUntil,
        });
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

    // Allow graceful shutdown
    void shutdown(const ShutdownParameters& /*params*/) override {
        closeBeforeShutdown();
    }

    void setState(bool shouldBeOpen) {
        if (shouldBeOpen) {
            open();
        } else {
            close();
        }
    }

    bool isOpen() {
        return state == ValveState::OPEN;
    }

private:
    void override(ValveState state, time_point<system_clock> until) {
        if (state == ValveState::NONE) {
            LOGI("Clearing override for valve '%s'", name.c_str());
        } else {
            LOGI("Overriding valve '%s' to state %d until %lld",
                name.c_str(), static_cast<int>(state), duration_cast<seconds>(until.time_since_epoch()).count());
        }
        updateQueue.put(OverrideSpec {
            .state = state,
            .until = until,
        });
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
