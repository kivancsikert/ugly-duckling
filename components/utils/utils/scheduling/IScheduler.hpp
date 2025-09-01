#pragma once

#include <chrono>
#include <optional>

#include <peripherals/valve/ValveScheduler.hpp> // For ValveState

namespace farmhub::utils::scheduling {

using ms = std::chrono::milliseconds;

enum class TargetState : int8_t {
    CLOSED = -1,
    OPEN = 1
};

struct ScheduleResult {
    // The state the scheduler decided to go for at this time, if any
    std::optional<TargetState> targetState;
    // Earliest time the scheduler needs to be called again (relative), or nullopt if ASAP
    std::optional<ms> nextDeadline;

    bool operator==(const ScheduleResult& other) const {
        return targetState == other.targetState && nextDeadline == other.nextDeadline;
    }
};

struct IScheduler {
    virtual ~IScheduler() = default;
    virtual ScheduleResult tick() = 0;
};


} // namespace farmhub::utils::scheduling
