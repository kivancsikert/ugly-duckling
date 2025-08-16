#pragma once

#include <chrono>
#include <concepts>
#include <limits>
#include <list>
#include <map>
#include <utility>
#include <variant>

#include <Concurrent.hpp>
#include <Named.hpp>
#include <Task.hpp>
#include <Telemetry.hpp>
#include <Watchdog.hpp>

#include <drivers/MotorDriver.hpp>
#include <drivers/SwitchManager.hpp>

#include <peripherals/Motors.hpp>
#include <peripherals/Peripheral.hpp>
#include <peripherals/light_sensor/Bh1750.hpp>
#include <peripherals/light_sensor/LightSensor.hpp>
#include <peripherals/light_sensor/Tsl2591.hpp>

using namespace farmhub::kernel;
using namespace farmhub::kernel::drivers;
using namespace farmhub::peripherals;
using namespace farmhub::peripherals::light_sensor;
using namespace std::chrono;
using namespace std::chrono_literals;

namespace farmhub::peripherals::chicken_door {

enum class DoorState : int8_t {
    INITIALIZED = -2,
    CLOSED = -1,
    NONE = 0,
    OPEN = 1
};

bool convertToJson(const DoorState& src, JsonVariant dst) {
    return dst.set(static_cast<int>(src));
}
void convertFromJson(JsonVariantConst src, DoorState& dst) {
    dst = static_cast<DoorState>(src.as<int>());
}

enum class OperationState : uint8_t {
    RUNNING,
    WATCHDOG_TIMEOUT
};

bool convertToJson(const OperationState& src, JsonVariant dst) {
    return dst.set(static_cast<int>(src));
}
void convertFromJson(JsonVariantConst src, OperationState& dst) {
    dst = static_cast<OperationState>(src.as<int>());
}

class ChickenDoorLightSensorSettings
    : public I2CSettings {
public:
    Property<std::string> type { this, "type", "bh1750" };
    Property<std::string> i2c { this, "i2c" };
    Property<seconds> measurementFrequency { this, "measurementFrequency", 1s };
    Property<seconds> latencyInterval { this, "latencyInterval", 5s };
};

class ChickenDoorSettings
    : public ConfigurationSection {
public:
    /**
     * @brief The motor to drive the door.
     */
    Property<std::string> motor { this, "motor" };

    /**
     * @brief Pin that indicates the door is open.
     */
    Property<InternalPinPtr> openPin { this, "openPin" };

    /**
     * @brief Pin that indicates the door is closed.
     */
    Property<InternalPinPtr> closedPin { this, "closedPin" };

    /**
     * @brief By default, open/close pins are high-active; set this to true to invert the logic.
     */
    Property<bool> invertSwitches { this, "invertSwitches", false };

    /**
     * @brief How long the motor is allowed to be running before we switch to emergency mode.
     */
    Property<seconds> movementTimeout { this, "movementTimeout", seconds(60) };

    /**
     * @brief Light sensor configuration.
     */
    NamedConfigurationEntry<ChickenDoorLightSensorSettings> lightSensor { this, "lightSensor" };
};

class ChickenDoorConfig : public ConfigurationSection {
public:
    /**
     * @brief Light level above which the door should be open.
     */
    Property<double> openLevel { this, "openLevel", 250 };

    /**
     * @brief Light level below which the door should be closed.
     */
    Property<double> closeLevel { this, "closeLevel", 10 };

    /**
     * @brief The state to override the schedule with.
     */
    Property<DoorState> overrideState { this, "overrideState", DoorState::NONE };

    /**
     * @brief Until when the override state is valid.
     */
    Property<time_point<system_clock>> overrideUntil { this, "overrideUntil" };
};

class ChickenDoorComponent final
    : Named
    , public HasConfig<ChickenDoorConfig>
    , public HasShutdown {
public:
    ChickenDoorComponent(
        const std::string& name,
        const std::shared_ptr<MqttRoot>& mqttRoot,
        const std::shared_ptr<SwitchManager>& switches,
        const std::shared_ptr<PwmMotorDriver>& motor,
        const std::shared_ptr<LightSensor>& lightSensor,
        const InternalPinPtr& openPin,
        const InternalPinPtr& closedPin,
        bool invertSwitches,
        ticks movementTimeout,
        const std::shared_ptr<TelemetryPublisher>& telemetryPublisher)
        : Named(name)
        , mqttRoot(mqttRoot)
        , motor(motor)
        , lightSensor(lightSensor)
        , openSwitch(switches->registerHandler(
              name + ":open",
              openPin,
              SwitchMode::PullUp,
              [this](const std::shared_ptr<Switch>&) { updateState(); },
              [this](const std::shared_ptr<Switch>&, milliseconds) { updateState(); }))
        , closedSwitch(switches->registerHandler(
              name + ":closed",
              closedPin,
              SwitchMode::PullUp,
              [this](const std::shared_ptr<Switch>&) { updateState(); },
              [this](const std::shared_ptr<Switch>&, milliseconds) { updateState(); }))
        , invertSwitches(invertSwitches)
        , watchdog(name + ":watchdog", movementTimeout, false, [this](WatchdogState state) {
            handleWatchdogEvent(state);
        })
        , telemetryPublisher(telemetryPublisher) {

        LOGI("Initializing chicken door %s, open switch %s, close switch %s%s",
            name.c_str(), openSwitch->getPin()->getName().c_str(), closedSwitch->getPin()->getName().c_str(),
            invertSwitches ? " (switches are inverted)" : "");

        motor->stop();

        mqttRoot->registerCommand("override", [this](const JsonObject& request, JsonObject& response) {
            auto overrideState = request["state"].as<DoorState>();
            auto overrideUntil = overrideState == DoorState::NONE
                ? time_point<system_clock>::min()
                : system_clock::now() + (request["duration"].is<JsonVariant>() ? request["duration"].as<seconds>() : 1h);
            updateQueue.put(ConfigureSpec {
                .openLevel = openLevel,
                .closeLevel = closeLevel,
                .overrideState = overrideState,
                .overrideUntil = overrideUntil,
            });
            response["overrideState"] = overrideState;
            response["overrideUntil"] = overrideUntil;
        });

        Task::run(name, 4096, 2, [this](Task& /*task*/) {
            runLoop();
        });
    }

    void populateTelemetry(JsonObject& telemetry) {
        Lock lock(stateMutex);
        telemetry["state"] = lastState;
        telemetry["targetState"] = lastTargetState;
        telemetry["operationState"] = operationState;
        if (overrideState != DoorState::NONE) {
            telemetry["overrideState"] = overrideState;
        }
    }

    void configure(const std::shared_ptr<ChickenDoorConfig>& config) override {
        configure(
            config->openLevel.get(),
            config->closeLevel.get(),
            config->overrideState.get(),
            config->overrideUntil.get());
    }

    void configure(double openLevel, double closeLevel, DoorState overrideState, time_point<system_clock> overrideUntil) {
        updateQueue.put(ConfigureSpec {
            .openLevel = openLevel,
            .closeLevel = closeLevel,
            .overrideState = overrideState,
            .overrideUntil = overrideUntil,
        });
    }

    void shutdown(const ShutdownParameters& /*params*/) override {
        // Stop movement and cancel watchdog; exit run loop
        motor->stop();
        watchdog.cancel();
        operationState = OperationState::WATCHDOG_TIMEOUT; // causes runLoop to exit
    }

private:
    void runLoop() {
        bool shouldPublishTelemetry = true;
        while (operationState == OperationState::RUNNING) {
            DoorState currentState = determineCurrentState();
            DoorState targetState = determineTargetState(currentState);
            if (currentState == DoorState::NONE && targetState == lastState) {
                // We have previously reached the target state, but we have lost the signal from the switches.
                // We assume the door is still in the target state to prevent it from moving when it shouldn't.
                currentState = lastState;
            }

            if (currentState != targetState) {
                if (currentState != lastState) {
                    LOGV("Going from state %d to %d (light level %.2f)",
                        static_cast<int>(currentState), static_cast<int>(targetState), lightSensor->getCurrentLevel());
                    watchdog.restart();
                }
                switch (targetState) {
                    case DoorState::OPEN:
                        motor->drive(MotorPhase::FORWARD, 1);
                        break;
                    case DoorState::CLOSED:
                        motor->drive(MotorPhase::REVERSE, 1);
                        break;
                    default:
                        motor->stop();
                        break;
                }
            } else {
                if (currentState != lastState) {
                    LOGV("Reached state %d (light level %.2f)",
                        static_cast<int>(currentState), lightSensor->getCurrentLevel());
                    watchdog.cancel();
                    motor->stop();
                    mqttRoot->publish("events/state", [=](JsonObject& json) { json["state"] = currentState; }, Retention::NoRetain, QoS::AtLeastOnce);
                }
            }

            {
                Lock lock(stateMutex);
                if (lastState != currentState || lastTargetState != targetState) {
                    lastState = currentState;
                    lastTargetState = targetState;
                    shouldPublishTelemetry = true;
                }
            }
            if (shouldPublishTelemetry) {
                telemetryPublisher->requestTelemetryPublishing();
                shouldPublishTelemetry = false;
            }

            auto now = system_clock::now();
            auto overrideWaitTime = overrideUntil < now
                ? ticks::max()
                : duration_cast<ticks>(overrideUntil - now);
            auto waitTime = std::min(overrideWaitTime, duration_cast<ticks>(lightSensor->getMeasurementFrequency()));
            updateQueue.pollIn(waitTime, [this, &shouldPublishTelemetry](auto& change) {
                std::visit(
                    [this, &shouldPublishTelemetry](auto&& arg) {
                        using T = std::decay_t<decltype(arg)>;
                        if constexpr (std::is_same_v<T, StateUpdated>) {
                            // State update received
                        } else if constexpr (std::is_same_v<T, ConfigureSpec>) {
                            Lock lock(stateMutex);

                            LOGI("Chicken door %s configured: open at %.2f lux, close at %.2f lux",
                                name.c_str(), arg.openLevel, arg.closeLevel);
                            this->openLevel = arg.openLevel;
                            this->closeLevel = arg.closeLevel;

                            if (arg.overrideState == DoorState::NONE) {
                                LOGI("Override cancelled");
                            } else {
                                LOGI("Override to %s, remaining duration: %lld sec",
                                    arg.overrideState == DoorState::OPEN ? "OPEN" : "CLOSED",
                                    duration_cast<seconds>(arg.overrideUntil - system_clock::now()).count());
                            }
                            overrideState = arg.overrideState;
                            overrideUntil = arg.overrideUntil;

                            shouldPublishTelemetry = true;
                        } else if constexpr (std::is_same_v<T, WatchdogTimeout>) {
                            LOGE("Watchdog timed out, stopping operation");
                            operationState = OperationState::WATCHDOG_TIMEOUT;
                            motor->stop();
                            shouldPublishTelemetry = true;
                        }
                    },
                    change);
            });
        }
    }

    void handleWatchdogEvent(WatchdogState state) {
        switch (state) {
            case WatchdogState::Started:
                LOGD("Watchdog started");
                sleepLock.emplace(PowerManager::noLightSleep);
                break;
            case WatchdogState::Cancelled:
                LOGD("Watchdog cancelled");
                sleepLock.reset();
                break;
            case WatchdogState::TimedOut:
                LOGD("Watchdog timed out");
                sleepLock.reset();
                updateQueue.offer(WatchdogTimeout {});
                break;
        }
    }

    void updateState() {
        updateQueue.offer(StateUpdated {});
    }

    DoorState determineCurrentState() {
        bool open = openSwitch->isEngaged() ^ invertSwitches;
        bool close = closedSwitch->isEngaged() ^ invertSwitches;
        if (open && close) {
            LOGD("Both open and close switches are engaged");
            return DoorState::NONE;
        }
        if (open) {
            return DoorState::OPEN;
        }
        if (close) {
            return DoorState::CLOSED;
        }
        return DoorState::NONE;
    }

    DoorState determineTargetState(DoorState currentState) {
        if (overrideState != DoorState::NONE) {
            if (overrideUntil >= system_clock::now()) {
                return overrideState;
            }
            LOGI("Override expired, returning to scheduled state");
            Lock lock(stateMutex);
            overrideState = DoorState::NONE;
            overrideUntil = time_point<system_clock>::min();
        }

        auto lightLevel = lightSensor->getCurrentLevel();
        if (lightLevel >= openLevel) {
            return DoorState::OPEN;
        }
        if (lightLevel <= closeLevel) {
            return DoorState::CLOSED;
        }
        return currentState == DoorState::NONE
            ? DoorState::CLOSED
            : currentState;
    }

    const std::shared_ptr<MqttRoot> mqttRoot;
    const std::shared_ptr<PwmMotorDriver> motor;
    const std::shared_ptr<LightSensor> lightSensor;

    double openLevel = std::numeric_limits<double>::max();
    double closeLevel = std::numeric_limits<double>::min();

    const std::shared_ptr<Switch> openSwitch;
    const std::shared_ptr<Switch> closedSwitch;
    const bool invertSwitches;

    Watchdog watchdog;

    const std::shared_ptr<TelemetryPublisher> telemetryPublisher;

    struct StateUpdated { };

    struct ConfigureSpec {
        double openLevel;
        double closeLevel;
        DoorState overrideState;
        time_point<system_clock> overrideUntil;
    };

    struct WatchdogTimeout { };

    Queue<std::variant<StateUpdated, ConfigureSpec, WatchdogTimeout>> updateQueue { "chicken-door-status", 2 };

    OperationState operationState = OperationState::RUNNING;

    Mutex stateMutex;
    DoorState lastState = DoorState::INITIALIZED;
    DoorState lastTargetState = DoorState::INITIALIZED;
    DoorState overrideState = DoorState::NONE;
    time_point<system_clock> overrideUntil = time_point<system_clock>::min();

    std::optional<PowerManagementLockGuard> sleepLock;
};

// (Adapter removed; ChickenDoorComponent now exposes configure(shared_ptr<ChickenDoorConfig>))

class NoLightSensor final
    : public LightSensor {
public:
    NoLightSensor(
        const std::string& name,
        const std::shared_ptr<I2CManager>& /*i2c*/,
        const I2CConfig& /*config*/,
        seconds measurementFrequency,
        seconds latencyInterval)
        : LightSensor(name, measurementFrequency, latencyInterval) {
        runLoop();
    }

protected:
    double readLightLevel() override {
        return -999;
    }
};

inline PeripheralFactory makeFactory(const std::map<std::string, std::shared_ptr<PwmMotorDriver>>& motors) {

    return makePeripheralFactory<ChickenDoorComponent, ChickenDoorSettings, ChickenDoorConfig>(
        "chicken-door",
        "chicken-door",
        [motors](PeripheralInitParameters& params, const std::shared_ptr<ChickenDoorSettings>& settings) {
            // Instantiate light sensor based on settings
            std::shared_ptr<LightSensor> lightSensor;
            const auto lightSensorType = settings->lightSensor.get()->type.get();
            try {
                if (lightSensorType == "bh1750") {
                    lightSensor = std::make_shared<Bh1750>(
                        params.name + ":light",
                        params.services.i2c,
                        settings->lightSensor.get()->parse(0x23),
                        settings->lightSensor.get()->measurementFrequency.get(),
                        settings->lightSensor.get()->latencyInterval.get());
                } else if (lightSensorType == "tsl2591") {
                    lightSensor = std::make_shared<Tsl2591>(
                        params.name + ":light",
                        params.services.i2c,
                        settings->lightSensor.get()->parse(TSL2591_ADDR),
                        settings->lightSensor.get()->measurementFrequency.get(),
                        settings->lightSensor.get()->latencyInterval.get());
                } else {
                    throw PeripheralCreationException("Unknown light sensor type: " + lightSensorType);
                }
            } catch (const std::exception& e) {
                LOGE("Could not initialize light sensor because %s", e.what());
                LOGW("Initializing without a light sensor");
                // Note: No I2C needed for NoLightSensor
                lightSensor = std::make_shared<NoLightSensor>(
                    params.name + ":light",
                    params.services.i2c,
                    settings->lightSensor.get()->parse(0x00),
                    settings->lightSensor.get()->measurementFrequency.get(),
                    settings->lightSensor.get()->latencyInterval.get());
            }

            auto motor = findMotor(motors, settings->motor.get());

            auto door = std::make_shared<ChickenDoorComponent>(
                params.name,
                params.mqttRoot,
                params.services.switches,
                motor,
                lightSensor,
                settings->openPin.get(),
                settings->closedPin.get(),
                settings->invertSwitches.get(),
                settings->movementTimeout.get(),
                params.services.telemetryPublisher);

            // Telemetry features
            params.registerFeature("light", [lightSensor](JsonObject& telemetryJson) {
                telemetryJson["value"] = lightSensor->getCurrentLevel();
            });
            params.registerFeature("door", [door](JsonObject& telemetryJson) {
                door->populateTelemetry(telemetryJson);
            });

            return door;
        });
}

}    // namespace farmhub::peripherals::chicken_door
