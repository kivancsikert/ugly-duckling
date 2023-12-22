#pragma once

#include <chrono>

using namespace std::chrono;

namespace farmhub { namespace peripherals {

class ValveSchedule {
public:
    ValveSchedule(
        tm start,
        seconds period,
        seconds duration)
        : start(start)
        , period(period)
        , duration(duration) {
    }

    ValveSchedule(const ValveSchedule& other)
        : start(other.start)
        , period(other.period)
        , duration(other.duration) {
    }

    const tm start;
    const seconds period;
    const seconds duration;
};

}}    // namespace farmhub::peripherals
