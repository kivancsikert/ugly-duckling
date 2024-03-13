#pragma once

#include <chrono>

#include <ArduinoJson.h>
#include <ArduinoLog.h>

#include <kernel/Configuration.hpp>
#include <peripherals/valve/ValveComponent.hpp>
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

static ValveControlStrategy* createValveControlStrategy(ValveControlStrategyType strategy, milliseconds switchDuration, double duty) {
    switch (strategy) {
        case ValveControlStrategyType::NormallyOpen:
            return new NormallyOpenValveControlStrategy(switchDuration, duty);
        case ValveControlStrategyType::NormallyClosed:
            return new NormallyClosedValveControlStrategy(switchDuration, duty);
        case ValveControlStrategyType::Latching:
            return new LatchingValveControlStrategy(switchDuration, duty);
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
    Property<double> duty { this, "duty", 100 };
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

}    // namespace farmhub::peripherals::valve

namespace ArduinoJson {

using farmhub::peripherals::valve::ValveSchedule;
template <>
struct Converter<ValveSchedule> {
    static void toJson(const ValveSchedule& src, JsonVariant dst) {
        JsonObject obj = dst.to<JsonObject>();
        auto startLocalTime = src.getStart();
        auto startTime = system_clock::to_time_t(startLocalTime);
        tm startTm;
        localtime_r(&startTime, &startTm);
        char buf[64];
        strftime(buf, sizeof(buf), "%FT%TZ", &startTm);
        obj["start"] = buf;
        obj["period"] = src.getPeriod().count();
        obj["duration"] = src.getDuration().count();
    }

    static ValveSchedule fromJson(JsonVariantConst src) {
        tm startTm;
        strptime(src["start"].as<const char*>(), "%FT%TZ", &startTm);
        auto startTime = mktime(&startTm);
        auto startLocalTime = system_clock::from_time_t(startTime);
        seconds period = seconds(src["period"].as<int>());
        seconds duration = seconds(src["duration"].as<int>());
        return ValveSchedule(startLocalTime, period, duration);
    }

    static bool checkJson(JsonVariantConst src) {
        return src["start"].is<const char*>()
            && src["period"].is<int>()
            && src["duration"].is<int>();
    }
};

}    // namespace ArduinoJson
