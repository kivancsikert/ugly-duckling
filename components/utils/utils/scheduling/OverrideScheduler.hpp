#pragma once

#include <chrono>
#include <optional>
#include <string>

#include <utils/scheduling/IScheduler.hpp>

namespace farmhub::utils::scheduling {

using namespace std::chrono;
using farmhub::peripherals::valve::ValveState;

class OverrideScheduler : public IScheduler {
public:
    void setOverride(TargetState state, time_point<system_clock> until) {
        overrideState = OverrideState {
            .state = state,
            .until = until,
        };
    }

    void clear() {
        overrideState.reset();
    }

    ScheduleResult tick() override {
        auto now = std::chrono::system_clock::now();
        if (overrideState.has_value()) {
            auto remaining = overrideState->until - now;
            if (remaining > 0ns) {
                return {
                    .targetState = overrideState->state,
                    .nextDeadline = duration_cast<ms>(remaining),
                };
            }
        }
        return {};
    }

private:
    struct OverrideState {
        TargetState state;
        time_point<system_clock> until;
    };

    std::optional<OverrideState> overrideState {};
};

}    // namespace farmhub::utils::scheduling
