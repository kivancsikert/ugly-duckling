#pragma once

#include <chrono>
#include <list>
#include <variant>

#include <driver/pcnt.h>

#include <Arduino.h>

#include <ArduinoLog.h>

#include <kernel/Component.hpp>
#include <kernel/Concurrent.hpp>
#include <kernel/Task.hpp>
#include <kernel/Telemetry.hpp>
#include <kernel/drivers/MotorDriver.hpp>
#include <kernel/drivers/SwitchManager.hpp>
#include <peripherals/Motorized.hpp>
#include <peripherals/Peripheral.hpp>
#include <peripherals/light_sensor/Bh1750.hpp>

using namespace farmhub::kernel;
using namespace farmhub::kernel::drivers;
using namespace farmhub::peripherals;
using namespace farmhub::peripherals::light_sensor;
using namespace std::chrono;
using namespace std::chrono_literals;
namespace farmhub::peripherals::chicken_door {

enum class DoorState {
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

class ChickenDoorDeviceConfig
    : public ConfigurationSection {
public:
    Property<String> motor { this, "motor" };
    Property<double> openLevel { this, "openLevel", 500 };
    Property<double> closeLevel { this, "closeLevel", 10 };
    Property<gpio_num_t> openPin { this, "openPin", GPIO_NUM_NC };
    Property<gpio_num_t> closedPin { this, "closedPin", GPIO_NUM_NC };

    NamedConfigurationEntry<Bh1750DeviceConfig> lightSensor { this, "lightSensor" };
};

class ChickenDoorComponent
    : public Component,
      public TelemetryProvider {
public:
    ChickenDoorComponent(
        const String& name,
        shared_ptr<MqttDriver::MqttRoot> mqttRoot,
        SleepManager& sleepManager,
        SwitchManager& switches,
        PwmMotorDriver& motor,
        Bh1750Component& lightSensor,
        double openLevel,
        double closeLevel,
        gpio_num_t openPin,
        gpio_num_t closedPin)
        : Component(name, mqttRoot)
        , sleepManager(sleepManager)
        , motor(motor)
        , lightSensor(lightSensor)
        , openLevel(openLevel)
        , closeLevel(closeLevel)
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
              [this](const Switch&, milliseconds) { updateState(); })) {

        Log.infoln("Initializing chicken door %s, open switch %d, close switch %d",
            name.c_str(), openSwitch.getPin(), closedSwitch.getPin());

        motor.stop();

        mqttRoot->registerCommand("override", [this](const JsonObject& request, JsonObject& response) {
            DoorState targetState = request["state"].as<DoorState>();
            if (targetState == DoorState::NONE) {
                updateQueue.put(StateOverride { DoorState::NONE, time_point<system_clock>::min() });
            } else {
                seconds duration = request.containsKey("duration")
                    ? request["duration"].as<seconds>()
                    : hours { 1 };
                updateQueue.put(StateOverride { targetState, system_clock::now() + duration });
                response["duration"] = duration;
            }
            response["state"] = state;
        });

        Task::loop(name, 4096, [&](Task& task) {
            DoorState currentState = determineCurrentState();
            DoorState targetState = determineTargetState(currentState);
            if (currentState != targetState) {
                if (currentState != lastState) {
                    Log.verboseln("Going from state %d to %d (light level %F)",
                        currentState, targetState, lightSensor.getCurrentLevel());
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
                    Log.verboseln("Staying in state %d (light level %F)",
                        currentState, lightSensor.getCurrentLevel());
                    motor.stop();
                }
            }
            lastState = currentState;
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
                            Log.infoln("Override received");
                            overrideState = arg.state;
                            overrideUntil = arg.until;
                        }
                    },
                    change);
            });
        });
    }

    void populateTelemetry(JsonObject& telemetry) override {
        telemetry["state"] = determineCurrentState();
        if (overrideState != DoorState::NONE) {
            telemetry["overrideState"] = overrideState;
            telemetry["overrideUntil"] = overrideUntil.time_since_epoch().count();
        }
    }

private:
    void updateState() {
        updateQueue.offer(StateUpdated {});
    }

    DoorState determineCurrentState() {
        bool open = openSwitch.isEngaged();
        bool close = closedSwitch.isEngaged();
        if (open && close) {
            Log.errorln("Both open and close switches are engaged");
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
                Log.infoln("Override expired, returning to scheduled state");
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
    PwmMotorDriver& motor;
    Bh1750Component& lightSensor;

    const double openLevel;
    const double closeLevel;

    const Switch& openSwitch;
    const Switch& closedSwitch;

    DoorState state = DoorState::NONE;

    struct StateUpdated {
    };

    struct StateOverride {
        DoorState state;
        time_point<system_clock> until;
    };

    Queue<std::variant<StateUpdated, StateOverride>> updateQueue { "chicken-door-status", 2 };

    DoorState lastState = DoorState::NONE;
    DoorState overrideState = DoorState::NONE;
    time_point<system_clock> overrideUntil = time_point<system_clock>::min();
};

class ChickenDoor
    : public Peripheral<EmptyConfiguration> {
public:
    ChickenDoor(
        const String& name,
        shared_ptr<MqttDriver::MqttRoot> mqttRoot,
        I2CManager& i2c,
        SleepManager& sleepManager,
        SwitchManager& switches,
        PwmMotorDriver& motor,
        const ChickenDoorDeviceConfig& config)
        : Peripheral<EmptyConfiguration>(name, mqttRoot)
        , lightSensor(
              name + ":light",
              mqttRoot,
              i2c,
              config.lightSensor.get().parse(0x23),
              config.lightSensor.get().measurementFrequency.get(),
              config.lightSensor.get().latencyInterval.get())
        , doorComponent(
              name,
              mqttRoot,
              sleepManager,
              switches,
              motor,
              lightSensor,
              config.openLevel.get(),
              config.closeLevel.get(),
              config.openPin.get(),
              config.closedPin.get()) {
    }

    void populateTelemetry(JsonObject& telemetryJson) override {
        lightSensor.populateTelemetry(telemetryJson);
        doorComponent.populateTelemetry(telemetryJson);
    }

private:
    Bh1750Component lightSensor;
    ChickenDoorComponent doorComponent;
};

class ChickenDoorFactory
    : public PeripheralFactory<ChickenDoorDeviceConfig, EmptyConfiguration>,
      protected Motorized {
public:
    ChickenDoorFactory(const std::list<ServiceRef<PwmMotorDriver>>& motors)
        : PeripheralFactory<ChickenDoorDeviceConfig, EmptyConfiguration>("chicken-door")
        , Motorized(motors) {
    }

    unique_ptr<Peripheral<EmptyConfiguration>> createPeripheral(const String& name, const ChickenDoorDeviceConfig& deviceConfig, shared_ptr<MqttDriver::MqttRoot> mqttRoot, PeripheralServices& services) override {
        PwmMotorDriver& motor = findMotor(deviceConfig.motor.get());
        return std::make_unique<ChickenDoor>(name, mqttRoot, services.i2c, services.sleepManager, services.switches, motor, deviceConfig);
    }
};

}    // namespace farmhub::peripherals::chicken_door
