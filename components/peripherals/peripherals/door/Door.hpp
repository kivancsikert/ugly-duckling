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
#include <peripherals/api/IDoor.hpp>

using namespace farmhub::kernel;
using namespace farmhub::kernel::drivers;
using namespace farmhub::peripherals;
using namespace farmhub::peripherals::api;
using namespace std::chrono;
using namespace std::chrono_literals;

namespace farmhub::peripherals::door {

LOGGING_TAG(DOOR, "door")

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
     * @brief By default, open/closed pins are high-active; set this to true to invert the logic.
     */
    Property<bool> invertSwitches { this, "invertSwitches", false };

    /**
     * @brief How long the motor is allowed to be running before we switch to emergency mode.
     */
    Property<seconds> movementTimeout { this, "movementTimeout", seconds(60) };
};

class Door final
    : public api::IDoor,
      public Peripheral,
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

        LOGTI(DOOR, "Initializing door %s, open switch %s, closed switch %s%s",
            name.c_str(), openSwitch->getPin()->getName().c_str(), closedSwitch->getPin()->getName().c_str(),
            invertSwitches ? " (switches are inverted)" : "");

        motor->stop();

        Task::run(name, 4096, 2, [this](Task& /*task*/) {
            runLoop();
        });
    }

    bool transitionTo(std::optional<TargetState> target) override {
        Lock lock(stateMutex);
        if (this->targetState == target) {
            return false;
        }
        updateQueue.put(ConfigureSpec { .targetState = target });
        return true;
    }

    DoorState getState() override {
        Lock lock(stateMutex);
        return lastState;
    }

    void populateTelemetry(JsonObject& telemetry) {
        Lock lock(stateMutex);
        telemetry["state"] = lastState;
        if (targetState) {
            telemetry["targetState"] = *targetState;
        }
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
            if (atTargetState(targetState, currentState)) {
                if (currentState != lastState) {
                    LOGTD(DOOR, "Door reached target state %d",
                        static_cast<int>(currentState));
                    watchdog.cancel();
                    motor->stop();
                    shouldPublishTelemetry = true;
                }
            } else if (targetState) {
                LOGTD(DOOR, "Door moving towards target state %s (current state %d)",
                    toString(targetState), static_cast<int>(currentState));
                switch (*targetState) {
                    case TargetState::Open:
                        motor->drive(MotorPhase::Forward, 1);
                        break;
                    case TargetState::Closed:
                        motor->drive(MotorPhase::Reverse, 1);
                        break;
                }
                watchdog.restart();
                shouldPublishTelemetry = true;
            } else {
                LOGTD(DOOR, "Door has no target state, stopping motor (current state %d)",
                    static_cast<int>(currentState));
                watchdog.cancel();
                motor->stop();
                shouldPublishTelemetry = true;
            }

            if (currentState != lastState) {
                Lock lock(stateMutex);
                lastState = currentState;
                shouldPublishTelemetry = true;
            }

            if (shouldPublishTelemetry) {
                telemetryPublisher->requestTelemetryPublishing();
                shouldPublishTelemetry = false;
            }

            updateQueue.take([&](auto& change) {
                std::visit(
                    [&](auto&& arg) {
                        using T = std::decay_t<decltype(arg)>;
                        if constexpr (std::is_same_v<T, StateUpdated>) {
                            LOGTV(DOOR, "Status update received");
                        } else if constexpr (std::is_same_v<T, ConfigureSpec>) {
                            Lock lock(stateMutex);
                            TargetState newTargetState = calculateEffectiveTargetState(arg.targetState, currentState);

                            if (!targetState || *targetState != newTargetState) {
                                LOGTI(DOOR, "Setting target state to %s (current state %d)",
                                    toString(newTargetState), static_cast<int>(currentState));
                                targetState = newTargetState;
                                shouldPublishTelemetry = true;
                            }
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
        LOGTW(DOOR, "Door '%s' exited run loop",
            name.c_str());
    }

    static bool
    atTargetState(std::optional<TargetState> targetState, DoorState state) {
        if (!targetState) {
            return false;
        }
        switch (*targetState) {
            case TargetState::Open:
                return state == DoorState::Open;
            case TargetState::Closed:
                return state == DoorState::Closed;
            default:
                throw std::invalid_argument("Unknown target state");
        }
    }

    static TargetState calculateEffectiveTargetState(std::optional<TargetState> newTargetState, DoorState currentState) {
        if (newTargetState) {
            return *newTargetState;
        }
        switch (currentState) {
            case DoorState::None:
                return TargetState::Closed;
            case DoorState::Open:
                return TargetState::Open;
            case DoorState::Closed:
                return TargetState::Closed;
            default:
                throw std::invalid_argument("Unknown door state");
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
        bool closed = closedSwitch->isEngaged() ^ invertSwitches;

        if (open && closed) {
            // TODO Handle this as a failure?
            LOGTW(DOOR, "Both open and closed switches are engaged, should the switches be inverted?");
            return DoorState::None;
        }
        if (open) {
            return DoorState::Open;
        }
        if (closed) {
            return DoorState::Closed;
        }

        if (atTargetState(targetState, lastState)) {
            // We have previously reached the target state, but we have likely lost the signal from the switches.
            // We assume the door is still in the target state to prevent it from moving when it shouldn't.
            return lastState;
        }

        return DoorState::None;
    }

    const std::shared_ptr<PwmMotorDriver> motor;

    const std::shared_ptr<Switch> openSwitch;
    const std::shared_ptr<Switch> closedSwitch;
    const bool invertSwitches;

    Watchdog watchdog;

    const std::shared_ptr<TelemetryPublisher> telemetryPublisher;

    struct StateUpdated { };

    struct ConfigureSpec {
        std::optional<TargetState> targetState;
    };

    struct WatchdogTimeout { };

    struct ShutdownSpec { };

    Queue<std::variant<StateUpdated, ConfigureSpec, WatchdogTimeout, ShutdownSpec>> updateQueue { "door-status", 8 };

    OperationState operationState = OperationState::Running;

    Mutex stateMutex;
    std::optional<TargetState> targetState;
    DoorState lastState = DoorState::None;

    std::optional<PowerManagementLockGuard> sleepLock;
};

inline PeripheralFactory makeFactory(const std::map<std::string, std::shared_ptr<PwmMotorDriver>>& motors) {

    return makePeripheralFactory<IDoor, Door, DoorSettings, EmptyConfiguration>(
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
