#pragma once

#include <chrono>
#include <list>

#include <kernel/Time.hpp>

using namespace std::chrono;
using namespace farmhub::kernel;

namespace farmhub::peripherals::valve {

enum class ValveState {
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

struct ValveStateUpdate {
    ValveState state;
    ticks transitionAfter;
};

class ValveScheduler {
public:
    static ValveStateUpdate getStateUpdate(std::list<ValveSchedule> schedules, time_point<system_clock> now, ValveState defaultState) {
        ValveStateUpdate next = { defaultState, ticks::max() };
        for (auto& schedule : schedules) {
            auto start = schedule.getStart();
            auto period = schedule.getPeriod();
            auto duration = schedule.getDuration();

            if (start > now) {
                continue;
            }

            auto diff = duration_cast<ticks>(now - start);
            auto periodPosition = diff % period;

            if (periodPosition < duration) {
                // We should be open
                auto transitionAfter = duration - periodPosition;
                if (next.state == ValveState::OPEN && transitionAfter < next.transitionAfter) {
                    continue;
                }
                next = { ValveState::OPEN, transitionAfter };
            } else {
                auto transitionAfter = period - periodPosition;
                if (next.state == ValveState::OPEN || transitionAfter > next.transitionAfter) {
                    continue;
                }
                next = { ValveState::CLOSED, transitionAfter };
            }
        }

        return next;
    }
};

}    // namespace farmhub::peripherals::valve
