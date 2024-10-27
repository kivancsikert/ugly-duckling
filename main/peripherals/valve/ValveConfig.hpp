#pragma once

#include <chrono>

#include <ArduinoJson.h>

#include <kernel/Configuration.hpp>
#include <kernel/Log.hpp>

#include <peripherals/valve/ValveComponent.hpp>
#include <peripherals/valve/ValveSchedule.hpp>
#include <peripherals/valve/ValveScheduler.hpp>

using namespace std::chrono;
using namespace std::chrono_literals;

using namespace farmhub::kernel;

namespace farmhub::peripherals::valve {

enum class ValveControlStrategyType {
    NormallyOpen,
    NormallyClosed,
    Latching
};

static ValveControlStrategy* createValveControlStrategy(PwmMotorDriver& motor, ValveControlStrategyType strategy, milliseconds switchDuration, double holdDuty) {
    switch (strategy) {
        case ValveControlStrategyType::NormallyOpen:
            return new NormallyOpenValveControlStrategy(motor, switchDuration, holdDuty);
        case ValveControlStrategyType::NormallyClosed:
            return new NormallyClosedValveControlStrategy(motor, switchDuration, holdDuty);
        case ValveControlStrategyType::Latching:
            return new LatchingValveControlStrategy(motor, switchDuration, holdDuty);
        default:
            throw std::runtime_error("Unknown strategy");
    }
}

class ValveConfig
    : public ConfigurationSection {
public:
    ArrayProperty<ValveSchedule> schedule { this, "schedule" };
};

class ValveDeviceConfig
    : public ConfigurationSection {
public:
    ValveDeviceConfig(ValveControlStrategyType defaultStrategy)
        : strategy(this, "strategy", defaultStrategy) {
    }

    Property<String> motor { this, "motor" };
    Property<ValveControlStrategyType> strategy;
    Property<double> holdDuty { this, "holdDuty", 100 };    // This is a percentage
    Property<milliseconds> switchDuration { this, "switchDuration", 500ms };
};

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
            Log.error("Unknown strategy: %d",
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
        Log.error("Unknown strategy: %s",
            strategy.c_str());
        dst = ValveControlStrategyType::NormallyClosed;
    }
}

}    // namespace farmhub::peripherals::valve
