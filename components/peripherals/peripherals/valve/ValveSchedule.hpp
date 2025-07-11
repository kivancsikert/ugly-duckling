#pragma once

#include <chrono>

#include <ArduinoJson.h>

using namespace std::chrono;
using namespace std::chrono_literals;

namespace farmhub::peripherals::valve {

enum class ValveState : int8_t {
    CLOSED = -1,
    NONE = 0,
    OPEN = 1
};

class ValveSchedule {
public:
    ValveSchedule(
        time_point<system_clock> start,
        seconds period,
        seconds duration)
        : start(start)
        , period(period)
        , duration(duration) {
    }

    constexpr time_point<system_clock> getStart() const {
        return start;
    }

    constexpr seconds getPeriod() const {
        return period;
    }

    constexpr seconds getDuration() const {
        return duration;
    }

private:
    time_point<system_clock> start;
    seconds period;
    seconds duration;
};

struct OverrideSchedule {
    ValveState state;
    time_point<system_clock> start;
    seconds duration;

    bool operator==(const OverrideSchedule& other) const {
        return state == other.state && start == other.start && duration == other.duration;
    }
};

}    // namespace farmhub::peripherals::valve

namespace ArduinoJson {

using farmhub::peripherals::valve::OverrideSchedule;
using farmhub::peripherals::valve::ValveSchedule;
using farmhub::peripherals::valve::ValveState;

template <>
struct Converter<system_clock::time_point> {
    static void toJson(system_clock::time_point src, JsonVariant dst) {
        time_t t = system_clock::to_time_t(src);
        tm tm {};
        localtime_r(&t, &tm);
        char buf[64];
        (void) strftime(buf, sizeof(buf), "%FT%TZ", &tm);
        dst.set(buf);
    }

    static system_clock::time_point fromJson(JsonVariantConst src) {
        tm tm {};
        strptime(src.as<const char*>(), "%FT%TZ", &tm);
        tm.tm_isdst = 0;
        return system_clock::from_time_t(mktime(&tm));
    }

    static bool checkJson(JsonVariantConst src) {
        return src.is<const char*>();
    }
};

template <>
struct Converter<ValveState> {
    static void toJson(const ValveState& src, JsonVariant dst) {
        dst.set(static_cast<int>(src));
    }

    static ValveState fromJson(JsonVariantConst src) {
        return static_cast<ValveState>(src.as<int>());
    }

    static bool checkJson(JsonVariantConst src) {
        return src.is<int>();
    }
};

template <>
struct Converter<ValveSchedule> {
    static void toJson(const ValveSchedule& src, JsonVariant dst) {
        JsonObject obj = dst.to<JsonObject>();
        obj["start"] = src.getStart();
        obj["period"] = src.getPeriod().count();
        obj["duration"] = src.getDuration().count();
    }

    static ValveSchedule fromJson(JsonVariantConst src) {
        auto start = src["start"].as<time_point<system_clock>>();
        auto period = seconds(src["period"].as<int64_t>());
        auto duration = seconds(src["duration"].as<int64_t>());
        return { start, period, duration };
    }

    static bool checkJson(JsonVariantConst src) {
        return src["start"].is<time_point<system_clock>>()
            && src["period"].is<int64_t>()
            && src["duration"].is<int64_t>();
    }
};

template <>
struct Converter<OverrideSchedule> {
    static void toJson(const OverrideSchedule& src, JsonVariant dst) {
        JsonObject obj = dst.to<JsonObject>();
        obj["state"] = src.state;
        obj["start"] = src.start;
        obj["duration"] = src.duration.count();
    }

    static OverrideSchedule fromJson(JsonVariantConst src) {
        auto state = src["state"].as<ValveState>();
        auto start = src["start"].as<time_point<system_clock>>();
        auto duration = seconds(src["duration"].as<int64_t>());
        return { state, start, duration };
    }

    static bool checkJson(JsonVariantConst src) {
        return src["state"].is<ValveState>()
            && src["start"].is<time_point<system_clock>>()
            && src["duration"].is<int64_t>();
    }
};

}    // namespace ArduinoJson
