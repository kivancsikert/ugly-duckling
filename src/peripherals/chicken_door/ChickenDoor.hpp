#pragma once

#include <chrono>
#include <limits>
#include <list>
#include <variant>

#include <driver/pcnt.h>

#include <Arduino.h>

#include <kernel/Component.hpp>
#include <kernel/Concurrent.hpp>
#include <kernel/Log.hpp>
#include <kernel/Task.hpp>
#include <kernel/Telemetry.hpp>
#include <kernel/Watchdog.hpp>

#include <kernel/drivers/CurrentSenseDriver.hpp>
#include <kernel/drivers/MotorDriver.hpp>
#include <kernel/drivers/SwitchManager.hpp>

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

enum class DoorState {
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

enum class OperationState {
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
    Property<String> type { this, "type", "bh1750" };
    Property<String> i2c { this, "i2c" };
    Property<seconds> measurementFrequency { this, "measurementFrequency", 1s };
    Property<seconds> latencyInterval { this, "latencyInterval", 5s };
};

class ChickenDoorDeviceConfig
    : public ConfigurationSection {
public:
    Property<String> motor { this, "motor" };
    Property<gpio_num_t> openPin { this, "openPin", GPIO_NUM_NC };
    Property<gpio_num_t> closedPin { this, "closedPin", GPIO_NUM_NC };
    Property<seconds> movementTimeout { this, "movementTimeout", seconds(60) };

    NamedConfigurationEntry<ChickenDoorLightSensorConfig> lightSensor { this, "lightSensor" };
};

class ChickenDoorConfig : public ConfigurationSection {
public:
    Property<double> openLevel { this, "openLevel", 250 };
    Property<double> closeLevel { this, "closeLevel", 10 };
};

template <typename TLightSensorComponent>
class ChickenDoorComponent
    : public Component,
      public TelemetryProvider {
public:
    ChickenDoorComponent(
        const String& name,
        shared_ptr<MqttDriver::MqttRoot> mqttRoot,
        SleepManager& sleepManager,
        SwitchManager& switches,
        CurrentSensingMotorDriver& motor,
        TLightSensorComponent& lightSensor,
        gpio_num_t openPin,
        gpio_num_t closedPin,
        ticks movementTimeout,
        std::function<void()> publishTelemetry)
        : Component(name, mqttRoot)
        , sleepManager(sleepManager)
        , motor(motor)
        , lightSensor(lightSensor)
        , openSwitch(switches.registerHandler(
              name + ":open",
              openPin,
              SwitchMode::PullUp,
              [this](const Switch&) { updateState(); },
              [this](const Switch&, milliseconds) { updateState(); }))
        , closedSwitch(switches.registerHandler(
              name + ":closed",
              closedPin,
              SwitchMode::PullUp,
              [this](const Switch&) { updateState(); },
              [this](const Switch&, milliseconds) { updateState(); }))
        , watchdog(name + ":watchdog", movementTimeout, [this](WatchdogState state) {
            handleWatchdogEvent(state);
        })
        , publishTelemetry(publishTelemetry)
    // TODO Make this configurable
    {

        Log.info("Initializing chicken door %s, open switch %d, close switch %d",
            name.c_str(), openSwitch.getPin(), closedSwitch.getPin());

        motor.stop();

        mqttRoot->registerCommand("override", [this](const JsonObject& request, JsonObject& response) {
            DoorState overrideState = request["state"].as<DoorState>();
            if (overrideState == DoorState::NONE) {
                updateQueue.put(StateOverride { DoorState::NONE, time_point<system_clock>::min() });
            } else {
                seconds duration = request.containsKey("duration")
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
        telemetry["current"] = motor.readCurrent();
        if (overrideState != DoorState::NONE) {
            time_t rawtime = system_clock::to_time_t(overrideUntil);
            auto timeinfo = gmtime(&rawtime);
            char buffer[80];
            strftime(buffer, 80, "%FT%TZ", timeinfo);
            telemetry["overrideEnd"] = String(buffer);
            telemetry["overrideState"] = overrideState;
        }
    }

    void configure(const ChickenDoorConfig& config) {
        openLevel = config.openLevel.get();
        closeLevel = config.closeLevel.get();
        Log.info("Configured chicken door %s to close at %.2f lux, and open at %.2f lux",
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

        if (currentState != DoorState::NONE) {
            Log.trace("Motor current: %.2f",
                motor.readCurrent());
        }

        if (currentState != targetState) {
            if (currentState != lastState) {
                Log.trace("Going from state %d to %d (light level %.2f)",
                    static_cast<int>(currentState), static_cast<int>(targetState), lightSensor.getCurrentLevel());
                watchdog.restart();
            }
            switch (targetState) {
                case DoorState::OPEN:
                    motor.drive(MotorPhase::FORWARD, 1);
                    break;
                case DoorState::CLOSED:
                    motor.drive(MotorPhase::REVERSE, 1);
                    break;
                default:
                    motor.stop();
                    break;
            }
        } else {
            if (currentState != lastState) {
                Log.trace("Reached state %d (light level %.2f)",
                    static_cast<int>(currentState), lightSensor.getCurrentLevel());
                watchdog.cancel();
                motor.stop();
                mqttRoot->publish("events/state", [=](JsonObject& json) {
                    json["state"] = currentState;
                });
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
                            Log.info("Override cancelled");
                        } else {
                            Log.info("Override received: %d duration: %lld sec",
                                static_cast<int>(arg.state), duration_cast<seconds>(arg.until - system_clock::now()).count());
                        }
                        {
                            Lock lock(stateMutex);
                            overrideState = arg.state;
                            overrideUntil = arg.until;
                        }
                        this->publishTelemetry();
                    } else if constexpr (std::is_same_v<T, WatchdogTimeout>) {
                        Log.error("Watchdog timeout, stopping operation");
                        operationState = OperationState::WATCHDOG_TIMEOUT;
                        motor.stop();
                        this->publishTelemetry();
                    }
                },
                change);
        });
    }

    void handleWatchdogEvent(WatchdogState state) {
        switch (state) {
            case WatchdogState::Started:
                Log.info("Watchdog started");
                sleepManager.keepAwake();
                break;
            case WatchdogState::Cacnelled:
                Log.info("Watchdog cancelled");
                sleepManager.allowSleep();
                break;
            case WatchdogState::TimedOut:
                Log.error("Watchdog timed out");
                sleepManager.allowSleep();
                updateQueue.offer(WatchdogTimeout {});
                break;
        }
    }

    void updateState() {
        updateQueue.offer(StateUpdated {});
    }

    DoorState determineCurrentState() {
        bool open = openSwitch.isEngaged();
        bool close = closedSwitch.isEngaged();
        if (open && close) {
            Log.error("Both open and close switches are engaged");
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
                Log.info("Override expired, returning to scheduled state");
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

    SleepManager& sleepManager;
    CurrentSensingMotorDriver& motor;
    TLightSensorComponent& lightSensor;

    double openLevel = std::numeric_limits<double>::max();
    double closeLevel = std::numeric_limits<double>::min();

    const Switch& openSwitch;
    const Switch& closedSwitch;

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
};

template <typename TLightSensorComponent>
class ChickenDoor
    : public Peripheral<ChickenDoorConfig> {
public:
    ChickenDoor(
        const String& name,
        shared_ptr<MqttDriver::MqttRoot> mqttRoot,
        I2CManager& i2c,
        uint8_t lightSensorAddress,
        SleepManager& sleepManager,
        SwitchManager& switches,
        CurrentSensingMotorDriver& motor,
        const ChickenDoorDeviceConfig& config)
        : Peripheral<ChickenDoorConfig>(name, mqttRoot)
        , lightSensor(
              name + ":light",
              mqttRoot,
              i2c,
              config.lightSensor.get().parse(lightSensorAddress),
              config.lightSensor.get().measurementFrequency.get(),
              config.lightSensor.get().latencyInterval.get())
        , doorComponent(
              name,
              mqttRoot,
              sleepManager,
              switches,
              motor,
              lightSensor,
              config.openPin.get(),
              config.closedPin.get(),
              config.movementTimeout.get(),
              [this]() {
                  publishTelemetry();
              }) {
    }

    void populateTelemetry(JsonObject& telemetryJson) override {
        lightSensor.populateTelemetry(telemetryJson);
        doorComponent.populateTelemetry(telemetryJson);
    }

    void configure(const ChickenDoorConfig& config) override {
        doorComponent.configure(config);
    }

private:
    TLightSensorComponent lightSensor;
    ChickenDoorComponent<TLightSensorComponent> doorComponent;
};

class NoLightSensorComponent : public LightSensorComponent {
public:
    NoLightSensorComponent(
        const String& name,
        shared_ptr<MqttDriver::MqttRoot> mqttRoot,
        I2CManager& i2c,
        I2CConfig config,
        seconds measurementFrequency,
        seconds latencyInterval)
        : LightSensorComponent(name, mqttRoot, measurementFrequency, latencyInterval) {
        runLoop();
    }

protected:
    double readLightLevel() override {
        return -999;
    }
};

class ChickenDoorFactory
    : public PeripheralFactory<ChickenDoorDeviceConfig, ChickenDoorConfig> {
public:
    ChickenDoorFactory(
        const ServiceContainer<CurrentSensingMotorDriver>& motors)
        : PeripheralFactory<ChickenDoorDeviceConfig, ChickenDoorConfig>("chicken-door")
        , motors(motors) {
    }

    unique_ptr<Peripheral<ChickenDoorConfig>> createPeripheral(const String& name, const ChickenDoorDeviceConfig& deviceConfig, shared_ptr<MqttDriver::MqttRoot> mqttRoot, PeripheralServices& services) override {
        CurrentSensingMotorDriver& motor = motors.findService(deviceConfig.motor.get());
        auto lightSensorType = deviceConfig.lightSensor.get().type.get();
        try {
            if (lightSensorType == "bh1750") {
                return std::make_unique<ChickenDoor<Bh1750Component>>(name, mqttRoot, services.i2c, 0x23, services.sleepManager, services.switches, motor, deviceConfig);
            } else if (lightSensorType == "tsl2591") {
                return std::make_unique<ChickenDoor<Tsl2591Component>>(name, mqttRoot, services.i2c, TSL2591_ADDR, services.sleepManager, services.switches, motor, deviceConfig);
            } else {
                throw PeripheralCreationException("Unknown light sensor type: " + lightSensorType);
            }
        } catch (const PeripheralCreationException& e) {
            Log.error("Could not initialize light sensor because %s", e.what());
            Log.warn("Initializing without a light sensor");
            // TODO Do not pass I2C parameters to NoLightSensorComponent
            return std::make_unique<ChickenDoor<NoLightSensorComponent>>(name, mqttRoot, services.i2c, 0x00, services.sleepManager, services.switches, motor, deviceConfig);
        }
    }

private:
    const ServiceContainer<CurrentSensingMotorDriver>& motors;
};

}    // namespace farmhub::peripherals::chicken_door
