#pragma once

#include <algorithm>
#include <chrono>
#include <list>
#include <optional>

#include <kernel/Time.hpp>
#include <peripherals/valve/ValveSchedule.hpp>

using namespace std::chrono;
using namespace farmhub::kernel;

namespace farmhub::peripherals::valve {

enum class ValveState {
    CLOSED = -1,
    NONE = 0,
    OPEN = 1
};

// JSON: ValveState

bool convertToJson(const ValveState& src, JsonVariant dst) {
    return dst.set(static_cast<int>(src));
}
void convertFromJson(JsonVariantConst src, ValveState& dst) {
    dst = static_cast<ValveState>(src.as<int>());
}

struct ValveStateUpdate {
    ValveState state;
    nanoseconds validFor;

    bool operator==(const ValveStateUpdate& other) const {
        return state == other.state && validFor == other.validFor;
    }
};

class ValveScheduler {
public:
    /**
     * @brief Determines the current valve state, and the next transition time based on given schedules and the current time.
     *
     * This function examines a list of valve schedules and the current time to decide the next state of the valve
     * and when the transition should occur. It accounts for overlapping schedules, preferring to keep the valve open
     * if any schedule demands it, and calculates the earliest necessary transition.
     *
     * @param schedules The list of ValveSchedule objects representing the valve's operating schedule.
     * @param now The current time_point.
     * @param defaultState The default state of the valve when no schedules apply.
     * @return ValveStateUpdate A structure indicating the current state of the valve, and the time after which the next transition should occur.
     */
    static ValveStateUpdate getStateUpdate(const std::list<ValveSchedule>& schedules, time_point<system_clock> now, ValveState defaultState) {
        auto targetState = ValveState::NONE;
        auto validFor = nanoseconds::max();

        for (const auto& schedule : schedules) {
            auto start = schedule.getStart();
            auto period = schedule.getPeriod();
            auto duration = schedule.getDuration();

            Log.info("Considering schedule starting at %lld (current time: %lld), period %lld, duration %lld",
                duration_cast<seconds>(start.time_since_epoch()).count(),
                duration_cast<seconds>(now.time_since_epoch()).count(),
                duration_cast<seconds>(period).count(),
                duration_cast<seconds>(duration).count());

            if (start > now) {
                // Schedule has not started yet; valve should be closed according to this schedule
                // Calculate when this schedule will start for the first time
                if (targetState != ValveState::OPEN) {
                    targetState = ValveState::CLOSED;
                    validFor = min(validFor, start - now);
                }
            } else {
                // This schedule has started; determine if the valve should be open or closed according to this schedule
                auto diff = now - start;

                // Damn you, C++ chrono, for not having a working modulo operator
                auto periodPosition = nanoseconds(duration_cast<nanoseconds>(diff).count() % duration_cast<nanoseconds>(period).count());
                Log.info("Diff: %lld sec, at: %lld sec, should be open until %lld / %lld sec",
                    duration_cast<seconds>(diff).count(),
                    duration_cast<seconds>(periodPosition).count(),
                    duration_cast<seconds>(duration).count(),
                    duration_cast<seconds>(period).count());

                if (periodPosition < duration) {
                    // The valve should be open according to this schedule
                    // Calculate when this opening period will end
                    nanoseconds closeAfter = duration - periodPosition;
                    if (targetState == ValveState::OPEN) {
                        // We already found a schedule to keep this valve open, extend the period if possible
                        validFor = max(validFor, closeAfter);
                    } else {
                        // This is the first schedule to keep the valve open
                        targetState = ValveState::OPEN;
                        validFor = closeAfter;
                    }
                } else {
                    // The valve should be closed according to this schedule
                    if (targetState != ValveState::OPEN) {
                        // There are no other schedules to keep the valve open yet,
                        // calculate when the next opening period will start
                        targetState = ValveState::CLOSED;
                        nanoseconds openAfter = period - periodPosition;
                        validFor = min(validFor, openAfter);
                    }
                }
            }
        }

        // If there are no schedules, return the default state with no transition
        if (targetState == ValveState::NONE) {
            targetState = defaultState;
        }

        return { targetState, validFor };
    }
};

}    // namespace farmhub::peripherals::valve
