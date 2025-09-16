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

using namespace farmhub::kernel;
using namespace farmhub::kernel::drivers;
using namespace farmhub::peripherals;
using namespace std::chrono;
using namespace std::chrono_literals;

namespace farmhub::peripherals::door {

LOGGING_TAG(DOOR, "door")

enum class DoorState : int8_t {
    Initialized = -2,
    Closed = -1,
    None = 0,
    Open = 1
};

bool convertToJson(const DoorState& src, JsonVariant dst) {
    return dst.set(static_cast<int>(src));
}
void convertFromJson(JsonVariantConst src, DoorState& dst) {
    dst = static_cast<DoorState>(src.as<int>());
}

enum class OperationState : uint8_t {
    Running,
    Stopped,
    WatchdogTimeout,
};

bool convertToJson(const OperationState& src, JsonVariant dst) {
    return dst.set(static_cast<int>(src));
}
void convertFromJson(JsonVariantConst src, OperationState& dst) {
    dst = static_cast<OperationState>(src.as<int>());
}

class DoorSettings final
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
};

class Door final
    : public Peripheral,
      public HasShutdown {
public:
    Door(
        const std::string& name,
        const std::shared_ptr<SwitchManager>& switches,
        const std::shared_ptr<PwmMotorDriver>& motor,
        const InternalPinPtr& openPin,
        const InternalPinPtr& closedPin,
        bool invertSwitches,
        ticks movementTimeout,
        const std::shared_ptr<TelemetryPublisher>& telemetryPublisher)
        : Peripheral(name)
        , motor(motor)
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

        LOGTI(DOOR, "Initializing door %s, open switch %s, close switch %s%s",
            name.c_str(), openSwitch->getPin()->getName().c_str(), closedSwitch->getPin()->getName().c_str(),
            invertSwitches ? " (switches are inverted)" : "");

        motor->stop();

        Task::run(name, 4096, 2, [this](Task& /*task*/) {
            runLoop();
        });
    }

    void setTarget(DoorState target) {
        updateQueue.put(ConfigureSpec { .targetState = target });
    }

    DoorState getState() {
        Lock lock(stateMutex);
        return lastState;
    }

    DoorState getTargetState() {
        Lock lock(stateMutex);
        return targetState;
    }

    void populateTelemetry(JsonObject& telemetry) {
        Lock lock(stateMutex);
        telemetry["state"] = lastState;
        telemetry["targetState"] = targetState;
        telemetry["operationState"] = operationState;
    }

    void shutdown(const ShutdownParameters& /*params*/) override {
        if (operationState == OperationState::Running) {
            updateQueue.put(ShutdownSpec {});
        }
    }

private:
    void runLoop() {
        bool shouldPublishTelemetry = true;
        while (operationState == OperationState::Running) {
            DoorState currentState = determineCurrentState();
            if (currentState == DoorState::None && targetState == lastState) {
                // We have previously reached the target state, but we have lost the signal from the switches.
                // We assume the door is still in the target state to prevent it from moving when it shouldn't.
                currentState = lastState;
            }

            if (currentState != targetState) {
                if (currentState != lastState) {
                    LOGTV(DOOR, "Going from state %d to %d (light level %.2f)",
                        static_cast<int>(currentState), static_cast<int>(targetState), lightSensor->getCurrentLevel());
                    watchdog.restart();
                }
                switch (targetState) {
                    case DoorState::Open:
                        motor->drive(MotorPhase::Forward, 1);
                        break;
                    case DoorState::Closed:
                        motor->drive(MotorPhase::Reverse, 1);
                        break;
                    default:
                        motor->stop();
                        break;
                }
            } else {
                if (currentState != lastState) {
                    LOGTV(DOOR, "Reached state %d (light level %.2f)",
                        static_cast<int>(currentState), lightSensor->getCurrentLevel());
                    watchdog.cancel();
                    motor->stop();
                    mqttRoot->publish("events/state", [=](JsonObject& json) { json["state"] = currentState; }, Retention::NoRetain, QoS::AtLeastOnce);
                }
            }

            {
                Lock lock(stateMutex);
                if (lastState != currentState) {
                    lastState = currentState;
                    shouldPublishTelemetry = true;
                }
            }
            if (shouldPublishTelemetry) {
                telemetryPublisher->requestTelemetryPublishing();
                shouldPublishTelemetry = false;
            }

            updateQueue.take([this, &shouldPublishTelemetry](auto& change) {
                std::visit(
                    [this, &shouldPublishTelemetry](auto&& arg) {
                        using T = std::decay_t<decltype(arg)>;
                        if constexpr (std::is_same_v<T, StateUpdated>) {
                            // State update received
                        } else if constexpr (std::is_same_v<T, ConfigureSpec>) {
                            Lock lock(stateMutex);
                            targetState = arg.targetState;
                            shouldPublishTelemetry = true;
                        } else if constexpr (std::is_same_v<T, WatchdogTimeout>) {
                            LOGTE(DOOR, "Watchdog timed out, stopping operation");
                            operationState = OperationState::WatchdogTimeout;
                            motor->stop();
                        } else if constexpr (std::is_same_v<T, ShutdownSpec>) {
                            LOGTI(DOOR, "Shutting down door operation");
                            operationState = OperationState::Stopped;
                            motor->stop();
                            watchdog.cancel();
                        }
                    },
                    change);
            });
        }
    }

    void handleWatchdogEvent(WatchdogState state) {
        switch (state) {
            case WatchdogState::Started:
                LOGTV(DOOR, "Watchdog started");
                sleepLock.emplace(PowerManager::noLightSleep);
                break;
            case WatchdogState::Cancelled:
                LOGTV(DOOR, "Watchdog cancelled");
                sleepLock.reset();
                break;
            case WatchdogState::TimedOut:
                LOGTV(DOOR, "Watchdog timed out");
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
            // TODO Handle this as a failure?
            LOGTW(DOOR, "Both open and close switches are engaged");
            return DoorState::None;
        }
        if (open) {
            return DoorState::Open;
        }
        if (close) {
            return DoorState::Closed;
        }
        return DoorState::None;
    }

    const std::shared_ptr<MqttRoot> mqttRoot;
    const std::shared_ptr<PwmMotorDriver> motor;
    const std::shared_ptr<LightSensor> lightSensor;

    const std::shared_ptr<Switch> openSwitch;
    const std::shared_ptr<Switch> closedSwitch;
    const bool invertSwitches;

    Watchdog watchdog;

    const std::shared_ptr<TelemetryPublisher> telemetryPublisher;

    struct StateUpdated { };

    struct ConfigureSpec {
        DoorState targetState;
    };

    struct WatchdogTimeout { };

    struct ShutdownSpec { };

    Queue<std::variant<StateUpdated, ConfigureSpec, WatchdogTimeout, ShutdownSpec>> updateQueue { "door-status", 8 };

    OperationState operationState = OperationState::Running;

    Mutex stateMutex;
    DoorState targetState = DoorState::Initialized;
    DoorState lastState = DoorState::Initialized;

    std::optional<PowerManagementLockGuard> sleepLock;
};

inline PeripheralFactory makeFactory(const std::map<std::string, std::shared_ptr<PwmMotorDriver>>& motors) {

    return makePeripheralFactory<Door, Door, DoorSettings, EmptyConfiguration>(
        "door",
        "door",
        [motors](PeripheralInitParameters& params, const std::shared_ptr<DoorSettings>& settings) {
            auto motor = findMotor(motors, settings->motor.get());

            auto door = std::make_shared<Door>(
                params.name,
                params.services.switches,
                motor,
                settings->openPin.get(),
                settings->closedPin.get(),
                settings->invertSwitches.get(),
                settings->movementTimeout.get(),
                params.services.telemetryPublisher);

            params.registerFeature("door", [door](JsonObject& telemetryJson) {
                door->populateTelemetry(telemetryJson);
            });

            return door;
        });
}

}    // namespace farmhub::peripherals::door
