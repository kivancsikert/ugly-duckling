#pragma once

#include <chrono>

#include <ArduinoJson.h>

using namespace std::chrono;
using namespace std::chrono_literals;

namespace farmhub::peripherals::valve {

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

    time_point<system_clock> getStart() const {
        return start;
    }

    seconds getPeriod() const {
        return period;
    }

    seconds getDuration() const {
        return duration;
    }

private:
    time_point<system_clock> start;
    seconds period;
    seconds duration;
};

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
