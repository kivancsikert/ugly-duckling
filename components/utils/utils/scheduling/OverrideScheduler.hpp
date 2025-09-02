#pragma once

#include <chrono>
#include <optional>
#include <string>

#include <utils/scheduling/IScheduler.hpp>

namespace farmhub::utils::scheduling {

using namespace std::chrono;

struct OverrideSchedule {
    TargetState state;
    time_point<system_clock> until;
};

class OverrideScheduler : public IScheduler {
public:
    void setOverride(const std::optional<OverrideSchedule>& schedule) {
        this->schedule = schedule;
    }

    ScheduleResult tick() override {
        auto now = std::chrono::system_clock::now();
        if (schedule.has_value()) {
            auto remaining = schedule->until - now;
            if (remaining <= 0ns) {
                schedule.reset();
                // We have no target state and it's fine to revisit us as late as possible,
                // but publish telemetry now as we no longer have an active override
                return {
                    .targetState = {},
                    .nextDeadline = {},
                    .shouldPublishTelemetry = true,
                };
            }
            return {
                .targetState = schedule->state,
                .nextDeadline = duration_cast<ms>(remaining),
                .shouldPublishTelemetry = false,
            };
        }
        // We have no target state and it's fine to revisit us as late as possible
        return {
            .targetState = {},
            .nextDeadline = {},
            .shouldPublishTelemetry = false,
        };
    }

private:
    std::optional<OverrideSchedule> schedule;
};

}    // namespace farmhub::utils::scheduling
