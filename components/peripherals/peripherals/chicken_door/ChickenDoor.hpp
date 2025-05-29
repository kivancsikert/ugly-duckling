#pragma once

#include <chrono>
#include <concepts>
#include <limits>
#include <list>
#include <utility>
#include <variant>

#include <Component.hpp>
#include <Concurrent.hpp>
#include <Task.hpp>
#include <Telemetry.hpp>
#include <Watchdog.hpp>

#include <drivers/MotorDriver.hpp>
#include <drivers/SwitchManager.hpp>

#include <peripherals/Motorized.hpp>
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

class ChickenDoorLightSensorConfig
    : public I2CDeviceConfig {
public:
    Property<std::string> type { this, "type", "bh1750" };
    Property<std::string> i2c { this, "i2c" };
    Property<seconds> measurementFrequency { this, "measurementFrequency", 1s };
    Property<seconds> latencyInterval { this, "latencyInterval", 5s };
};

class ChickenDoorDeviceConfig
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
    NamedConfigurationEntry<ChickenDoorLightSensorConfig> lightSensor { this, "lightSensor" };
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
};

template <std::derived_from<LightSensorComponent> TLightSensorComponent>
class ChickenDoorComponent
    : public Component,
      public TelemetryProvider {
public:
    ChickenDoorComponent(
        const std::string& name,
        std::shared_ptr<MqttRoot> mqttRoot,
        std::shared_ptr<SwitchManager> switches,
        const std::shared_ptr<PwmMotorDriver>& motor,
        TLightSensorComponent& lightSensor,
        InternalPinPtr openPin,
        InternalPinPtr closedPin,
        bool invertSwitches,
        ticks movementTimeout,
        std::function<void()> publishTelemetry)
        : Component(name, mqttRoot)
        , motor(motor)
        , lightSensor(lightSensor)
        , openSwitch(switches->registerHandler(
              name + ":open",
              openPin,
              SwitchMode::PullUp,
              [this](const Switch&) { updateState(); },
              [this](const Switch&, milliseconds) { updateState(); }))
        , closedSwitch(switches->registerHandler(
              name + ":closed",
              closedPin,
              SwitchMode::PullUp,
              [this](const Switch&) { updateState(); },
              [this](const Switch&, milliseconds) { updateState(); }))
        , invertSwitches(invertSwitches)
        , watchdog(name + ":watchdog", movementTimeout, false, [this](WatchdogState state) {
            handleWatchdogEvent(state);
        })
        , publishTelemetry(std::move(publishTelemetry))
    // TODO Make this configurable
    {

        LOGI("Initializing chicken door %s, open switch %s, close switch %s%s",
            name.c_str(), openSwitch.getPin()->getName().c_str(), closedSwitch.getPin()->getName().c_str(),
            invertSwitches ? " (switches are inverted)" : "");

        motor->stop();

        mqttRoot->registerCommand("override", [this](const JsonObject& request, JsonObject& response) {
            DoorState overrideState = request["state"].as<DoorState>();
            if (overrideState == DoorState::NONE) {
                updateQueue.put(StateOverride { DoorState::NONE, time_point<system_clock>::min() });
            } else {
                seconds duration = request["duration"].is<JsonVariant>()
                    ? request["duration"].as<seconds>()
                    : hours { 1 };
                updateQueue.put(StateOverride { overrideState, system_clock::now() + duration });
                response["duration"] = duration;
            }
            response["overrideState"] = overrideState;
        });

        Task::run(name, 4096, 2, [this](Task& task) {
            while (operationState == OperationState::RUNNING) {
                runLoop(task);
            }
        });
    }

    void populateTelemetry(JsonObject& telemetry) override {
        Lock lock(stateMutex);
        telemetry["state"] = lastState;
        telemetry["targetState"] = lastTargetState;
        telemetry["operationState"] = operationState;
        if (overrideState != DoorState::NONE) {
            time_t rawtime = system_clock::to_time_t(overrideUntil);
            auto timeinfo = gmtime(&rawtime);
            char buffer[80];
            strftime(buffer, 80, "%FT%TZ", timeinfo);
            telemetry["overrideEnd"] = std::string(buffer);
            telemetry["overrideState"] = overrideState;
        }
    }

    void configure(const std::shared_ptr<ChickenDoorConfig>& config) {
        openLevel = config->openLevel.get();
        closeLevel = config->closeLevel.get();
        LOGI("Configured chicken door %s to close at %.2f lux, and open at %.2f lux",
            name.c_str(), closeLevel, openLevel);
    }

private:
    void runLoop(Task& task) {
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
                    static_cast<int>(currentState), static_cast<int>(targetState), lightSensor.getCurrentLevel());
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
                    static_cast<int>(currentState), lightSensor.getCurrentLevel());
                watchdog.cancel();
                motor->stop();
                mqttRoot->publish("events/state", [=](JsonObject& json) { json["state"] = currentState; }, Retention::NoRetain, QoS::AtLeastOnce);
            }
        }

        bool shouldPublishTelemetry = false;
        {
            Lock lock(stateMutex);
            if (lastState != currentState || lastTargetState != targetState) {
                lastState = currentState;
                lastTargetState = targetState;
                shouldPublishTelemetry = true;
            }
        }
        if (shouldPublishTelemetry) {
            publishTelemetry();
        }

        auto now = system_clock::now();
        auto overrideWaitTime = overrideUntil < now
            ? ticks::max()
            : duration_cast<ticks>(overrideUntil - now);
        auto waitTime = std::min(overrideWaitTime, duration_cast<ticks>(lightSensor.getMeasurementFrequency()));
        updateQueue.pollIn(waitTime, [this](auto& change) {
            std::visit(
                [this](auto&& arg) {
                    using T = std::decay_t<decltype(arg)>;
                    if constexpr (std::is_same_v<T, StateUpdated>) {
                        // State update received
                    } else if constexpr (std::is_same_v<T, StateOverride>) {
                        if (arg.state == DoorState::NONE) {
                            LOGI("Override cancelled");
                        } else {
                            LOGI("Override received: %d duration: %lld sec",
                                static_cast<int>(arg.state), duration_cast<seconds>(arg.until - system_clock::now()).count());
                        }
                        {
                            Lock lock(stateMutex);
                            overrideState = arg.state;
                            overrideUntil = arg.until;
                        }
                        this->publishTelemetry();
                    } else if constexpr (std::is_same_v<T, WatchdogTimeout>) {
                        LOGE("Watchdog timed out, stopping operation");
                        operationState = OperationState::WATCHDOG_TIMEOUT;
                        motor->stop();
                        this->publishTelemetry();
                    }
                },
                change);
        });
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
        bool open = openSwitch.isEngaged() ^ invertSwitches;
        bool close = closedSwitch.isEngaged() ^ invertSwitches;
        if (open && close) {
            LOGD("Both open and close switches are engaged");
            return DoorState::NONE;
        } else if (open) {
            return DoorState::OPEN;
        } else if (close) {
            return DoorState::CLOSED;
        } else {
            return DoorState::NONE;
        }
    }

    DoorState determineTargetState(DoorState currentState) {
        if (overrideUntil >= system_clock::now()) {
            return overrideState;
        } else {
            if (overrideState != DoorState::NONE) {
                LOGI("Override expired, returning to scheduled state");
                Lock lock(stateMutex);
                overrideState = DoorState::NONE;
                overrideUntil = time_point<system_clock>::min();
            }
            auto lightLevel = lightSensor.getCurrentLevel();
            if (lightLevel >= openLevel) {
                return DoorState::OPEN;
            } else if (lightLevel <= closeLevel) {
                return DoorState::CLOSED;
            }
            return currentState == DoorState::NONE
                ? DoorState::CLOSED
                : currentState;
        }
    }

    const std::shared_ptr<PwmMotorDriver> motor;
    TLightSensorComponent& lightSensor;

    double openLevel = std::numeric_limits<double>::max();
    double closeLevel = std::numeric_limits<double>::min();

    const Switch& openSwitch;
    const Switch& closedSwitch;
    const bool invertSwitches;

    Watchdog watchdog;

    const std::function<void()> publishTelemetry;

    struct StateUpdated { };

    struct StateOverride {
        DoorState state;
        time_point<system_clock> until;
    };

    struct WatchdogTimeout { };

    Queue<std::variant<StateUpdated, StateOverride, WatchdogTimeout>> updateQueue { "chicken-door-status", 2 };

    OperationState operationState = OperationState::RUNNING;

    Mutex stateMutex;
    DoorState lastState = DoorState::INITIALIZED;
    DoorState lastTargetState = DoorState::INITIALIZED;
    DoorState overrideState = DoorState::NONE;
    time_point<system_clock> overrideUntil = time_point<system_clock>::min();

    std::optional<PowerManagementLockGuard> sleepLock;
};

template <std::derived_from<LightSensorComponent> TLightSensorComponent>
class ChickenDoor
    : public Peripheral<ChickenDoorConfig> {
public:
    ChickenDoor(
        const std::string& name,
        std::shared_ptr<MqttRoot> mqttRoot,
        std::shared_ptr<I2CManager> i2c,
        uint8_t lightSensorAddress,
        std::shared_ptr<SwitchManager> switches,
        const std::shared_ptr<PwmMotorDriver> motor,
        const std::shared_ptr<ChickenDoorDeviceConfig> config)
        : Peripheral<ChickenDoorConfig>(name, mqttRoot)
        , lightSensor(
              name + ":light",
              mqttRoot,
              i2c,
              config->lightSensor.get()->parse(lightSensorAddress),
              config->lightSensor.get()->measurementFrequency.get(),
              config->lightSensor.get()->latencyInterval.get())
        , doorComponent(
              name,
              mqttRoot,
              switches,
              motor,
              lightSensor,
              config->openPin.get(),
              config->closedPin.get(),
              config->invertSwitches.get(),
              config->movementTimeout.get(),
              [this]() {
                  publishTelemetry();
              }) {
    }

    void populateTelemetry(JsonObject& telemetryJson) override {
        lightSensor.populateTelemetry(telemetryJson);
        doorComponent.populateTelemetry(telemetryJson);
    }

    void configure(const std::shared_ptr<ChickenDoorConfig> config) override {
        doorComponent.configure(config);
    }

private:
    TLightSensorComponent lightSensor;
    ChickenDoorComponent<TLightSensorComponent> doorComponent;
};

class NoLightSensorComponent : public LightSensorComponent {
public:
    NoLightSensorComponent(
        const std::string& name,
        std::shared_ptr<MqttRoot> mqttRoot,
        const std::shared_ptr<I2CManager>& i2c,
        const I2CConfig& config,
        seconds measurementFrequency,
        seconds latencyInterval)
        : LightSensorComponent(name, std::move(mqttRoot), measurementFrequency, latencyInterval) {
        runLoop();
    }

protected:
    double readLightLevel() override {
        return -999;
    }
};

class ChickenDoorFactory
    : public PeripheralFactory<ChickenDoorDeviceConfig, ChickenDoorConfig>,
      protected Motorized {
public:
    ChickenDoorFactory(const std::map<std::string, std::shared_ptr<PwmMotorDriver>>& motors)
        : PeripheralFactory<ChickenDoorDeviceConfig, ChickenDoorConfig>("chicken-door")
        , Motorized(motors) {
    }

    std::unique_ptr<Peripheral<ChickenDoorConfig>> createPeripheral(const std::string& name, const std::shared_ptr<ChickenDoorDeviceConfig> deviceConfig, std::shared_ptr<MqttRoot> mqttRoot, const PeripheralServices& services) override {
        std::shared_ptr<PwmMotorDriver> motor = findMotor(deviceConfig->motor.get());
        auto lightSensorType = deviceConfig->lightSensor.get()->type.get();
        try {
            if (lightSensorType == "bh1750") {
                return std::make_unique<ChickenDoor<Bh1750Component>>(name, mqttRoot, services.i2c, 0x23, services.switches, motor, deviceConfig);
            } else if (lightSensorType == "tsl2591") {
                return std::make_unique<ChickenDoor<Tsl2591Component>>(name, mqttRoot, services.i2c, TSL2591_ADDR, services.switches, motor, deviceConfig);
            } else {
                throw PeripheralCreationException("Unknown light sensor type: " + lightSensorType);
            }
        } catch (const std::exception& e) {
            LOGE("Could not initialize light sensor because %s", e.what());
            LOGW("Initializing without a light sensor");
            // TODO Do not pass I2C parameters to NoLightSensorComponent
            return std::make_unique<ChickenDoor<NoLightSensorComponent>>(name, mqttRoot, services.i2c, 0x00, services.switches, motor, deviceConfig);
        }
    }
};

}    // namespace farmhub::peripherals::chicken_door
