#pragma once

#include <chrono>

using namespace std::chrono;

namespace farmhub { namespace peripherals { namespace valve {

enum class ValveState {
    CLOSED = -1,
    NONE = 0,
    OPEN = 1
};

class ValveSchedule {
public:
    ValveSchedule(
        const tm& start,
        seconds period,
        seconds duration)
        : start(start)
        , period(period)
        , duration(duration) {
    }

    const tm& getStart() const {
        return start;
    }
    seconds getPeriod() const {
        return period;
    }
    seconds getDuration() const {
        return duration;
    }

private:
    const tm start;
    const seconds period;
    const seconds duration;
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
            tm scheduleTm = schedule.getStart();
            auto scheduleStart = mktime(&scheduleTm);
            auto scheduleStartLocalTime = system_clock::from_time_t(scheduleStart);

            auto period = schedule.getPeriod();
            auto duration = schedule.getDuration();

            if (scheduleStartLocalTime > now) {
                continue;
            }

            auto diff = duration_cast<ticks>(now - scheduleStartLocalTime);
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

}}}    // namespace farmhub::peripherals::valve
