#pragma once

#include <algorithm>
#include <chrono>
#include <list>
#include <optional>

#include <Log.hpp>
#include <Time.hpp>
#include <peripherals/valve/ValveSchedule.hpp>

using namespace std::chrono;
using namespace farmhub::kernel;

namespace farmhub::peripherals::valve {

struct ValveStateDecision {
    ValveState state;
    nanoseconds expiresAfter;

    bool operator==(const ValveStateDecision& other) const {
        return state == other.state && expiresAfter == other.expiresAfter;
    }
};

class ValveScheduler {
public:
    /**
     * @brief Determines what state the valve should be in and how long to keep that state
     * based on a schedule and a previously determined state and expiration.
     *
     * @param schedules The list of ValveSchedule objects representing the valve's operating schedule.
     * @param now The current time_point.
     * @param defaultState The default state of the valve when no schedules apply.
     * @return ValveStateDecision A structure indicating the current state of the valve, and the time after which the next transition should occur.
     */
    static std::optional<ValveStateDecision> updateValveStateDecision(std::optional<ValveStateDecision> previousDecision, const time_point<system_clock> start, seconds activeDuration, seconds period, ValveState targetState, time_point<system_clock> now) {
        LOGI("Considering schedule to %s starting at %lld s (current time: %lld s), duration %lld s, period %lld s",
            targetState == ValveState::OPEN ? "open" : (targetState == ValveState::CLOSED ? "close" : "UNKNOWN"),
            duration_cast<seconds>(start.time_since_epoch()).count(),
            duration_cast<seconds>(now.time_since_epoch()).count(),
            duration_cast<seconds>(activeDuration).count(),
            duration_cast<seconds>(period).count());

        auto timeSinceScheduleStart = now - start;
        if (timeSinceScheduleStart < nanoseconds::zero()) {
            // Schedule has not started yet
            auto timeUntilScheduleStart = -timeSinceScheduleStart;
            if (!previousDecision.has_value()) {
                // No previous transition, re-check at the start time
                return ValveStateDecision { ValveState::NONE, timeUntilScheduleStart };
            } else if (previousDecision->state == targetState && previousDecision->expiresAfter >= timeUntilScheduleStart) {
                // The previous transition is for the same target state, and it will expire after the schedule starts
                return ValveStateDecision { targetState, std::max(previousDecision->expiresAfter, timeUntilScheduleStart + activeDuration) };
            } else {
                // There is a previous transition, check again after the earlier of the two times passed
                return ValveStateDecision { previousDecision->state, std::min(previousDecision->expiresAfter, timeUntilScheduleStart) };
            }
        } else {
            // Schedule has started, determine if it is currently active

            // A zero period means a single-shot schedule, no periodicity
            auto timeSincePeriodStart = period == seconds::zero()
                ? timeSinceScheduleStart
                // Damn you, C++ chrono, for not having a working modulo operator
                : nanoseconds(duration_cast<nanoseconds>(timeSinceScheduleStart).count() % duration_cast<nanoseconds>(period).count());

            if (timeSincePeriodStart < activeDuration) {
                // This schedule is currently active
                nanoseconds activityEndsAfter = activeDuration - timeSincePeriodStart;
                if (!previousDecision.has_value()) {
                    // No previous transition
                    return ValveStateDecision { targetState, activityEndsAfter };
                }

                if (previousDecision->state == ValveState::NONE) {
                    // The previous schedule hasn't started yet, we can stay in the target state until then, or the end of this period, whichever is earlier
                    return ValveStateDecision { targetState, std::min(previousDecision->expiresAfter, activityEndsAfter) };
                }

                if (previousDecision->state == targetState) {
                    // We already found a schedule that has the same target state, we only need to check after both periods end
                    return ValveStateDecision { targetState, std::max(previousDecision->expiresAfter, activityEndsAfter) };
                }

                // We've already determined a different target state, and that should take precedence
                return previousDecision;
            } else if (period > seconds::zero()) {
                // This schedule is currently inactive, wait until the next period starts
                nanoseconds nextPeriodStartsAfter = period - timeSincePeriodStart;
                if (!previousDecision.has_value()) {
                    // No previous transition, re-check at the start of the next period
                    return ValveStateDecision { ValveState::NONE, nextPeriodStartsAfter };
                }

                if (previousDecision->state == ValveState::NONE) {
                    // The previous schedule hasn't started yet, we can stay in the NONE state until the next period starts
                    return ValveStateDecision { ValveState::NONE, std::min(previousDecision->expiresAfter, nextPeriodStartsAfter) };
                }

                // We already need to be in a state, let's stay there until the next period starts, or the previous state expires, whichever is earlier
                return ValveStateDecision { previousDecision->state, std::min(previousDecision->expiresAfter, nextPeriodStartsAfter) };
            } else {
                // This is a non-periodic schedule that has already finished, we have no transition to make
                return std::nullopt;
            }
        }
    }
};

}    // namespace farmhub::peripherals::valve
