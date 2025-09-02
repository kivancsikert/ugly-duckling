#pragma once

#include <algorithm>
#include <chrono>
#include <list>
#include <optional>

#include <utils/scheduling/IScheduler.hpp>

namespace farmhub::utils::scheduling {

using namespace std::chrono;

struct TimeBasedSchedule {
    time_point<system_clock> start;
    seconds period;
    seconds duration;
};

class TimeBasedScheduler : public IScheduler {
public:
    TimeBasedScheduler() = default;

    void setSchedules(const std::list<TimeBasedSchedule>& newSchedules) {
        schedules = newSchedules;
    }

    /**
     * @brief Determines the current valve state, and the next transition time based on given schedules and the current time.
     *
     * This function examines a list of valve schedules and the current time to decide the next state of the valve
     * and when the transition should occur. It accounts for overlapping schedules, preferring to keep the valve open
     * if any schedule demands it, and calculates the earliest necessary transition.
     *
     * @param schedules The list of TimeBasedSchedule objects representing the valve's operating schedule.
     * @param now The current time_point.
     * @return A structure indicating the current target state of the valve, and the time after which the next transition should occur.
     */
    static ScheduleResult getStateUpdate(const std::list<TimeBasedSchedule>& schedules, time_point<system_clock> now) {
        auto targetState = std::optional<TargetState>();
        auto validFor = nanoseconds::max();

        for (const auto& schedule : schedules) {
            // #ifndef GTEST
            //             LOGI("Considering schedule starting at %lld (current time: %lld), period %lld, duration %lld",
            //                 duration_cast<seconds>(schedule.start.time_since_epoch()).count(),
            //                 duration_cast<seconds>(now.time_since_epoch()).count(),
            //                 duration_cast<seconds>(schedule.period).count(),
            //                 duration_cast<seconds>(schedule.duration).count());
            // #endif

            if (schedule.start > now) {
                // Schedule has not started yet; valve should be closed according to this schedule
                // Calculate when this schedule will start for the first time
                if (!targetState.has_value() || *targetState != TargetState::OPEN) {
                    targetState = TargetState::CLOSED;
                    validFor = std::min(validFor, schedule.start - now);
                }
            } else {
                // This schedule has started; determine if the valve should be open or closed according to this schedule
                auto diff = now - schedule.start;

                // Damn you, C++ chrono, for not having a working modulo operator
                auto periodPosition = nanoseconds(duration_cast<nanoseconds>(diff).count() % duration_cast<nanoseconds>(schedule.period).count());
                // #ifndef GTEST
                //                 LOGI("Diff: %lld sec, at: %lld sec, should be open until %lld / %lld sec",
                //                     duration_cast<seconds>(diff).count(),
                //                     duration_cast<seconds>(periodPosition).count(),
                //                     duration_cast<seconds>(schedule.duration).count(),
                //                     duration_cast<seconds>(schedule.period).count());
                // #endif

                if (periodPosition < schedule.duration) {
                    // The valve should be open according to this schedule
                    // Calculate when this opening period will end
                    nanoseconds closeAfter = schedule.duration - periodPosition;
                    if (targetState.has_value() && *targetState == TargetState::OPEN) {
                        // We already found a schedule to keep this valve open, extend the period if possible
                        validFor = std::max(validFor, closeAfter);
                    } else {
                        // This is the first schedule to keep the valve open
                        targetState = TargetState::OPEN;
                        validFor = closeAfter;
                    }
                } else {
                    // The valve should be closed according to this schedule
                    if (targetState.has_value() && *targetState != TargetState::OPEN) {
                        // There are no other schedules to keep the valve open yet,
                        // calculate when the next opening period will start
                        targetState = TargetState::CLOSED;
                        nanoseconds openAfter = schedule.period - periodPosition;
                        validFor = std::min(validFor, openAfter);
                    }
                }
            }
        }

        return {
            .targetState = targetState,
            .nextDeadline = duration_cast<milliseconds>(validFor),
            .shouldPublishTelemetry = false,
        };
    }

    ScheduleResult tick() override {
        return getStateUpdate(schedules, std::chrono::system_clock::now());
    }

private:
    std::list<TimeBasedSchedule> schedules;
};

}    // namespace farmhub::utils::scheduling

namespace ArduinoJson {

using farmhub::utils::scheduling::TimeBasedSchedule;

template <>
struct Converter<TimeBasedSchedule> {
    static void toJson(const TimeBasedSchedule& src, JsonVariant dst) {
        JsonObject obj = dst.to<JsonObject>();
        obj["start"] = src.start;
        obj["period"] = src.period.count();
        obj["duration"] = src.duration.count();
    }

    static TimeBasedSchedule fromJson(JsonVariantConst src) {
        auto start = src["start"].as<time_point<system_clock>>();
        auto period = seconds(src["period"].as<int64_t>());
        auto duration = seconds(src["duration"].as<int64_t>());
        return { .start = start, .period = period, .duration = duration };
    }

    static bool checkJson(JsonVariantConst src) {
        return src["start"].is<time_point<system_clock>>()
            && src["period"].is<int64_t>()
            && src["duration"].is<int64_t>();
    }
};

}    // namespace ArduinoJson
