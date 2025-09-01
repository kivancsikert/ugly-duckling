#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <time.h>

#include <utils/scheduling/TimeBasedScheduler.hpp>

using namespace std::chrono;
using namespace std::chrono_literals;

using namespace farmhub::utils::scheduling;
static const auto T0 = system_clock::from_time_t(1704067200); // 2024-01-01 00:00:00 UTC

TEST_CASE("not scheduled when empty") {
    ScheduleResult update = TimeBasedScheduler::getStateUpdate({}, T0);
    REQUIRE(update == ScheduleResult { std::nullopt, milliseconds::max() });
}

TEST_CASE("keeps closed until schedule starts") {
    std::list<TimeBasedSchedule> schedules {
        TimeBasedSchedule { .start = T0, .period = 1h, .duration = 15s },
    };
    REQUIRE(TimeBasedScheduler::getStateUpdate(schedules, T0 - 1s) == ScheduleResult { TargetState::CLOSED, 1s });
}

TEST_CASE("keeps open when schedule is started and in period") {
    std::list<TimeBasedSchedule> schedules {
        TimeBasedSchedule { .start = T0, .period = 1h, .duration = 15s },
    };
    REQUIRE(TimeBasedScheduler::getStateUpdate(schedules, T0) == ScheduleResult { TargetState::OPEN, 15s });
    REQUIRE(TimeBasedScheduler::getStateUpdate(schedules, T0 + 1s) == ScheduleResult { TargetState::OPEN, 14s });
}

TEST_CASE("keeps closed when schedule is started and outside period") {
    std::list<TimeBasedSchedule> schedules {
        TimeBasedSchedule { .start = T0, .period = 1h, .duration = 15s },
    };
    REQUIRE(TimeBasedScheduler::getStateUpdate(schedules, T0 + 15s) == ScheduleResult { TargetState::CLOSED, 1h - 15s });
    REQUIRE(TimeBasedScheduler::getStateUpdate(schedules, T0 + 16s) == ScheduleResult { TargetState::CLOSED, 1h - 16s });
}

TEST_CASE("when there are overlapping schedules keep closed until earliest opens") {
    // --OOOOOO--------------
    // ----OOOOOO------------
    std::list<TimeBasedSchedule> schedules {
        TimeBasedSchedule { .start = T0 + 5min, .period = 1h, .duration = 15min },
        TimeBasedSchedule { .start = T0 + 10min, .period = 1h, .duration = 15min },
    };
    // Keep closed until first schedule starts
    REQUIRE(TimeBasedScheduler::getStateUpdate(schedules, T0) == ScheduleResult { TargetState::CLOSED, 5min });
    REQUIRE(TimeBasedScheduler::getStateUpdate(schedules, T0 + 1s) == ScheduleResult { TargetState::CLOSED, 5min - 1s });
}

TEST_CASE("when there are overlapping schedules keep open until latest closes") {
    // --OOOOOO--------------
    // ----OOOOOO------------
    std::list<TimeBasedSchedule> schedules {
        TimeBasedSchedule { .start = T0 + 5min, .period = 1h, .duration = 15min },
        TimeBasedSchedule { .start = T0 + 10min, .period = 1h, .duration = 15min },
    };
    // Open when first schedule starts, and keep open
    REQUIRE(TimeBasedScheduler::getStateUpdate(schedules, T0 + 5min) == ScheduleResult { TargetState::OPEN, 15min });
    REQUIRE(TimeBasedScheduler::getStateUpdate(schedules, T0 + 5min + 1s) == ScheduleResult { TargetState::OPEN, 15min - 1s });
    REQUIRE(TimeBasedScheduler::getStateUpdate(schedules, T0 + 10min) == ScheduleResult { TargetState::OPEN, 15min });
    REQUIRE(TimeBasedScheduler::getStateUpdate(schedules, T0 + 15min) == ScheduleResult { TargetState::OPEN, 10min });
    REQUIRE(TimeBasedScheduler::getStateUpdate(schedules, T0 + 25min - 1s) == ScheduleResult { TargetState::OPEN, 1s });

    // Close again after later schedule ends, and reopen when first schedule starts again
    REQUIRE(TimeBasedScheduler::getStateUpdate(schedules, T0 + 25min) == ScheduleResult { TargetState::CLOSED, 40min });
    REQUIRE(TimeBasedScheduler::getStateUpdate(schedules, T0 + 25min + 1s) == ScheduleResult { TargetState::CLOSED, 40min - 1s });
}

TEST_CASE("handles back-to-back schedules without gap") {
    // Two schedules that touch end-to-start: [0..10s) and [10s..20s)
    std::list<TimeBasedSchedule> schedules{
        TimeBasedSchedule { .start = T0, .period = 30s, .duration = 10s },
        TimeBasedSchedule { .start = T0 + 10s, .period = 30s, .duration = 10s },
    };

    // At start => OPEN for 10s
    REQUIRE(TimeBasedScheduler::getStateUpdate(schedules, T0) == ScheduleResult{TargetState::OPEN, 10s});
    // Just before switch => still OPEN
    REQUIRE(TimeBasedScheduler::getStateUpdate(schedules, T0 + 10s - 1ms) == ScheduleResult{TargetState::OPEN, 1ms});
    // Exactly at boundary => next schedule keeps it OPEN for another 10s
    REQUIRE(TimeBasedScheduler::getStateUpdate(schedules, T0 + 10s) == ScheduleResult{TargetState::OPEN, 10s});
    // After second ends => CLOSED until next period
    REQUIRE(TimeBasedScheduler::getStateUpdate(schedules, T0 + 20s) == ScheduleResult{TargetState::CLOSED, 10s});
}

TEST_CASE("stays closed until first open, then reverts correctly") {
    std::list<TimeBasedSchedule> schedules{
        TimeBasedSchedule { .start = T0 + 5s, .period = 60s, .duration = 2s },
    };

    // Before first start => NONE
    REQUIRE(TimeBasedScheduler::getStateUpdate(schedules, T0) == ScheduleResult{TargetState::CLOSED, 5s});
    // During open => OPEN
    REQUIRE(TimeBasedScheduler::getStateUpdate(schedules, T0 + 5s + 500ms) == ScheduleResult{TargetState::OPEN, 1500ms});
    // After close => NONE until next period
    REQUIRE(TimeBasedScheduler::getStateUpdate(schedules, T0 + 7s) == ScheduleResult{TargetState::CLOSED, 58s});
}

TEST_CASE("non-overlapping sequences alternate open and closed as expected") {
    std::list<TimeBasedSchedule> schedules{
        TimeBasedSchedule { .start = T0 + 0s, .period = 20s, .duration = 5s },
        TimeBasedSchedule { .start = T0 + 10s, .period = 20s, .duration = 5s },
    };

    // 0..5s OPEN, 5..10s CLOSED, 10..15s OPEN, 15..20s CLOSED
    REQUIRE(TimeBasedScheduler::getStateUpdate(schedules, T0 + 0s) == ScheduleResult{TargetState::OPEN, 5s});
    REQUIRE(TimeBasedScheduler::getStateUpdate(schedules, T0 + 5s) == ScheduleResult{TargetState::CLOSED, 5s});
    REQUIRE(TimeBasedScheduler::getStateUpdate(schedules, T0 + 10s) == ScheduleResult{TargetState::OPEN, 5s});
    REQUIRE(TimeBasedScheduler::getStateUpdate(schedules, T0 + 15s) == ScheduleResult{TargetState::CLOSED, 5s});
}
