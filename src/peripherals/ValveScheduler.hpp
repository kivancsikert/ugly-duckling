#pragma once

#include <chrono>
#include <date.h>
#include <iostream>
#include <sstream>
#include <list>

#include <ArduinoJson.h>

using std::chrono::duration_cast;
using std::chrono::seconds;
using std::chrono::system_clock;
using std::chrono::time_point;

namespace farmhub { namespace peripherals {

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

    ValveSchedule(
        const char* start,
        seconds period,
        seconds duration)
        : ValveSchedule(parseIsoDate(start), period, duration) {
    }

    ValveSchedule(const JsonObject& json)
        : ValveSchedule(
            json["start"].as<const char*>(),
            seconds { json["period"].as<int>() },
            seconds { json["duration"].as<int>() }) {
    }

    void print() const {
        std::cout << "start: " << date::format("%FT%TZ", start) << std::endl;
        std::cout << "period: " << period.count() << " seconds" << std::endl;
        std::cout << "duration: " << duration.count() << " seconds" << std::endl;
    }

    const time_point<system_clock> start;
    const seconds period;
    const seconds duration;

private:
    static time_point<system_clock> parseIsoDate(const char* value) {
        std::istringstream in(value);
        time_point<system_clock> date;
        in >> date::parse("%FT%TZ", date);
        return date;
    }
};

class ValveScheduler {
public:
    ValveScheduler() = default;

    bool isScheduled(const std::list<ValveSchedule>& schedules, time_point<system_clock> time) {
        for (auto& schedule : schedules) {
            if (time < schedule.start) {
                // Skip schedules that have not yet started
                continue;
            }
            auto offset = time - schedule.start;
            if (offset % schedule.period < schedule.duration) {
                return true;
            }
        }
        return false;
    }
};

}}    // namespace farmhub::peripherals
