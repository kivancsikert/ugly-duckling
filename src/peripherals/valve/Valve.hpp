#pragma once

#include <chrono>
#include <memory>

#include <Arduino.h>

#include <ArduinoJson.h>
#include <ArduinoLog.h>

#include <devices/Peripheral.hpp>
#include <kernel/Service.hpp>
#include <kernel/Task.hpp>
#include <kernel/Telemetry.hpp>
#include <kernel/drivers/MotorDriver.hpp>

#include <peripherals/valve/ValveScheduler.hpp>

using namespace std::chrono;
using std::make_unique;
using std::move;
using std::unique_ptr;

using namespace farmhub::devices;
using namespace farmhub::kernel::drivers;

namespace farmhub { namespace peripherals { namespace valve {

enum class ValveState {
    CLOSED = -1,
    NONE = 0,
    OPEN = 1
};

enum class ValveControlStrategyType {
    NormallyOpen,
    NormallyClosed,
    Latching
};

class ValveControlStrategy {
public:
    virtual void open(PwmMotorDriver& controller) = 0;
    virtual void close(PwmMotorDriver& controller) = 0;
    virtual ValveState getDefaultState() = 0;

    virtual String describe() = 0;
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
                controller.drive(MotorPhase::FORWARD, holdDuty);
                break;
            case ValveState::CLOSED:
                controller.drive(MotorPhase::REVERSE, holdDuty);
                break;
            default:
                // Ignore
                break;
        }
        delay(switchDuration.count());
        controller.stop();
    }

    const milliseconds switchDuration;
    const double holdDuty;
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

    ValveState getDefaultState() override {
        return ValveState::CLOSED;
    }

    String describe() override {
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

    ValveState getDefaultState() override {
        return ValveState::OPEN;
    }

    String describe() override {
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

    ValveState getDefaultState() override {
        return ValveState::NONE;
    }

    String describe() override {
        return "latching with switch duration " + String((int) switchDuration.count()) + "ms with switch duty " + String(switchDuty * 100) + "%";
    }

private:
    const milliseconds switchDuration;
    const double switchDuty;
};

class ValveConfig
    : public ConfigurationSection {
public:
    Property<milliseconds> frequency { this, "frequency", seconds(15000) };
    ObjectArrayProperty<ValveSchedule> schedule { this, "schedule" };
};

class Valve
    : public Peripheral<ValveConfig> {
public:
    Valve(const String& name, PwmMotorDriver& controller, ValveControlStrategy& strategy)
        : Peripheral<ValveConfig>(name)
        , controller(controller)
        , strategy(strategy) {
        Log.infoln("Creating valve '%s' with strategy %s",
            name.c_str(), strategy.describe().c_str());

        controller.stop();

        // TODO Restore stored state
        setState(strategy.getDefaultState());
    }

    void configure(const ValveConfig& config) override {
        if (task != nullptr) {
            task->abort();
        }
        Log.infoln("Configuring valve '%s' with frequency %d",
            name.c_str(), config.frequency.get().count());
        task = new TaskHandle(Task::loop(name, [this, &config](Task& task) {
            open();
            task.delayUntil(config.frequency.get() / 2);
            close();
            task.delayUntil(config.frequency.get() / 2);
        }));
    }

    void open() {
        Log.traceln("Opening valve");
        strategy.open(controller);
        this->state = ValveState::OPEN;
    }

    void close() {
        Log.traceln("Closing valve");
        strategy.close(controller);
        this->state = ValveState::CLOSED;
    }

    void reset() {
        Log.traceln("Resetting valve");
        controller.stop();
    }

    void setState(ValveState state) {
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
        // TODO Publish event
    }

    void populateTelemetry(JsonObject& telemetry) override {
        telemetry["state"] = this->state;
    }

private:
    PwmMotorDriver& controller;
    ValveControlStrategy& strategy;

    ValveState state = ValveState::NONE;
    TaskHandle* task = nullptr;
};

class ValveDeviceConfig
    : public ConfigurationSection {
public:
    ValveDeviceConfig(ValveControlStrategyType defaultStrategy)
        : strategy(this, "strategy", defaultStrategy) {
    }

    Property<String> motor { this, "motor" };
    Property<ValveControlStrategyType> strategy;
    Property<double> duty { this, "duty", 100 };
    Property<milliseconds> switchDuration { this, "switchDuration", milliseconds(500) };
};

class ValveFactory
    : public PeripheralFactory<ValveDeviceConfig, ValveConfig> {
public:
    ValveFactory(const std::list<ServiceRef<PwmMotorDriver>>& motors, ValveControlStrategyType defaultStrategy)
        : PeripheralFactory<ValveDeviceConfig, ValveConfig>("valve")
        , motors(motors)
        , defaultStrategy(defaultStrategy) {
    }

    ValveDeviceConfig* createDeviceConfig() override {
        return new ValveDeviceConfig(defaultStrategy);
    }

    Valve* createPeripheral(const String& name, const ValveDeviceConfig& deviceConfig) override {
        PwmMotorDriver* targetMotor = nullptr;
        for (auto& motor : motors) {
            if (motor.getName() == deviceConfig.motor.get()) {
                targetMotor = &(motor.get());
                break;
            }
        }
        if (targetMotor == nullptr) {
            // TODO Add proper error handling
            Log.errorln("Failed to find motor: %s",
                deviceConfig.motor.get().c_str());
            return nullptr;
        }
        ValveControlStrategy* strategy = createStrategy(deviceConfig);
        if (strategy == nullptr) {
            // TODO Add proper error handling
            Log.errorln("Failed to create strategy");
            return nullptr;
        }
        return new Valve(name, *targetMotor, *strategy);
    }

private:
    ValveControlStrategy* createStrategy(const ValveDeviceConfig& config) {
        auto switchDuration = config.switchDuration.get();
        auto duty = config.duty.get() / 100.0;
        switch (config.strategy.get()) {
            case ValveControlStrategyType::NormallyOpen:
                return new NormallyOpenValveControlStrategy(switchDuration, duty);
            case ValveControlStrategyType::NormallyClosed:
                return new NormallyClosedValveControlStrategy(switchDuration, duty);
            case ValveControlStrategyType::Latching:
                return new LatchingValveControlStrategy(switchDuration, duty);
            default:
                // TODO Add proper error handling
                return nullptr;
        }
    }

    const std::list<ServiceRef<PwmMotorDriver>> motors;
    const ValveControlStrategyType defaultStrategy;
};

// JSON: ValveState

bool convertToJson(const ValveState& src, JsonVariant dst) {
    return dst.set(static_cast<int>(src));
}
void convertFromJson(JsonVariantConst src, ValveState& dst) {
    dst = static_cast<ValveState>(src.as<int>());
}

// JSON: ValveControlStrategyType

bool convertToJson(const ValveControlStrategyType& src, JsonVariant dst) {
    switch (src) {
        case ValveControlStrategyType::NormallyOpen:
            return dst.set("NO");
        case ValveControlStrategyType::NormallyClosed:
            return dst.set("NC");
        case ValveControlStrategyType::Latching:
            return dst.set("latching");
        default:
            Log.errorln("Unknown strategy: %d",
                static_cast<int>(src));
            return dst.set("NC");
    }
}
void convertFromJson(JsonVariantConst src, ValveControlStrategyType& dst) {
    String strategy = src.as<String>();
    if (strategy == "NO") {
        dst = ValveControlStrategyType::NormallyOpen;
    } else if (strategy == "NC") {
        dst = ValveControlStrategyType::NormallyClosed;
    } else if (strategy == "latching") {
        dst = ValveControlStrategyType::Latching;
    } else {
        Log.errorln("Unknown strategy: %s",
            strategy.c_str());
        dst = ValveControlStrategyType::NormallyClosed;
    }
}

}}}    // namespace farmhub::peripherals::valve

namespace ArduinoJson {

using farmhub::peripherals::valve::ValveSchedule;
template <>
struct Converter<ValveSchedule> {
    static void toJson(const ValveSchedule& src, JsonVariant dst) {
        JsonObject obj = dst.to<JsonObject>();
        char buf[64];
        strftime(buf, sizeof(buf), "%FT%TZ", &src.start);
        obj["start"] = buf;
        obj["period"] = src.period.count();
        obj["duration"] = src.duration.count();
    }

    static ValveSchedule fromJson(JsonVariantConst src) {
        tm start;
        strptime(src["start"].as<const char*>(), "%FT%TZ", &start);
        seconds period = seconds(src["period"].as<int>());
        seconds duration = seconds(src["duration"].as<int>());
        return ValveSchedule(start, period, duration);
    }

    static bool checkJson(JsonVariantConst src) {
        return src["start"].is<const char*>()
            && src["period"].is<int>()
            && src["duration"].is<int>();
    }
};

}    // namespace ArduinoJson
