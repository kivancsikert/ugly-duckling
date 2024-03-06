#include <gtest/gtest.h>

// TODO Move this someplace else?
#define configTICK_RATE_HZ 1000

#include <chrono>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <time.h>

#include <ArduinoJson.h>

#include <peripherals/valve/ValveScheduler.hpp>

using namespace std::chrono;
using namespace std::chrono_literals;

namespace farmhub::peripherals::valve {

static time_point<system_clock> parseTime(const char* str) {
    tm time;
    std::istringstream ss(str);
    ss >> std::get_time(&time, "%Y-%m-%d %H:%M:%S");
    return system_clock::from_time_t(mktime(&time));
}

std::ostream& operator<<(std::ostream& os, const ValveState& val) {
    switch (val) {
        case ValveState::CLOSED:
            os << "CLOSED";
            break;
        case ValveState::NONE:
            os << "NONE";
            break;
        case ValveState::OPEN:
            os << "OPEN";
            break;
    }
    return os;
}

std::ostream& operator<<(std::ostream& os, const ticks& val) {
    os << duration_cast<milliseconds>(val).count() << " ms";
    return os;
}

std::ostream& operator<<(std::ostream& os, const ValveStateUpdate& val) {
    os << "{ state: " << val.state << ", transitionAfter: " << val.validFor << " }";
    return os;
}

class ValveSchedulerTest : public testing::Test {
public:
    time_point<system_clock> base = parseTime("2024-01-01 00:00:00");
    ValveScheduler scheduler;
};

TEST_F(ValveSchedulerTest, can_create_schedule) {
    ValveSchedule schedule(base, 1h, 1min);
    EXPECT_EQ(schedule.getStart(), base);
    EXPECT_EQ(schedule.getPeriod(), 1h);
    EXPECT_EQ(schedule.getDuration(), 1min);
}

TEST_F(ValveSchedulerTest, not_scheduled_when_empty) {
    for (ValveState defaultState : { ValveState::CLOSED, ValveState::NONE, ValveState::OPEN }) {
        ValveStateUpdate update = scheduler.getStateUpdate({}, base, defaultState);
        EXPECT_EQ(update, (ValveStateUpdate { defaultState, ticks::max() }));
    }
}

TEST_F(ValveSchedulerTest, keeps_closed_until_schedule_starts) {
    std::list<ValveSchedule> schedules {
        ValveSchedule(base, 1h, 15s),
    };
    for (ValveState defaultState : { ValveState::CLOSED, ValveState::NONE, ValveState::OPEN }) {
        EXPECT_EQ(scheduler.getStateUpdate(schedules, base - 1s, defaultState), (ValveStateUpdate { ValveState::CLOSED, 1s }));
    }
}

TEST_F(ValveSchedulerTest, keeps_open_when_schedule_is_started_and_in_period) {
    std::list<ValveSchedule> schedules {
        ValveSchedule(base, 1h, 15s),
    };
    for (ValveState defaultState : { ValveState::CLOSED, ValveState::NONE, ValveState::OPEN }) {
        EXPECT_EQ(scheduler.getStateUpdate(schedules, base, defaultState), (ValveStateUpdate { ValveState::OPEN, 15s }));
        EXPECT_EQ(scheduler.getStateUpdate(schedules, base + 1s, defaultState), (ValveStateUpdate { ValveState::OPEN, 14s }));
    }
}

TEST_F(ValveSchedulerTest, keeps_closed_when_schedule_is_started_and_outside_period) {
    std::list<ValveSchedule> schedules {
        ValveSchedule(base, 1h, 15s),
    };
    for (ValveState defaultState : { ValveState::CLOSED, ValveState::NONE, ValveState::OPEN }) {
        EXPECT_EQ(scheduler.getStateUpdate(schedules, base + 15s, defaultState), (ValveStateUpdate { ValveState::CLOSED, 1h - 15s }));
        EXPECT_EQ(scheduler.getStateUpdate(schedules, base + 16s, defaultState), (ValveStateUpdate { ValveState::CLOSED, 1h - 16s }));
    }
}

TEST_F(ValveSchedulerTest, when_there_are_overlapping_schedules_keep_closed_until_earliest_opens) {
    // --OOOOOO--------------
    // ----OOOOOO------------
    std::list<ValveSchedule> schedules {
        ValveSchedule(base + 5min, 1h, 15min),
        ValveSchedule(base + 10min, 1h, 15min),
    };
    for (ValveState defaultState : { ValveState::CLOSED, ValveState::NONE, ValveState::OPEN }) {
        // Keep closed until first schedule starts
        EXPECT_EQ(scheduler.getStateUpdate(schedules, base, defaultState), (ValveStateUpdate { ValveState::CLOSED, 5min }));
        EXPECT_EQ(scheduler.getStateUpdate(schedules, base + 1s, defaultState), (ValveStateUpdate { ValveState::CLOSED, 5min - 1s }));
    }
}

TEST_F(ValveSchedulerTest, when_there_are_overlapping_schedules_keep_open_until_latest_closes) {
    // --OOOOOO--------------
    // ----OOOOOO------------
    std::list<ValveSchedule> schedules {
        ValveSchedule(base + 5min, 1h, 15min),
        ValveSchedule(base + 10min, 1h, 15min),
    };
    for (ValveState defaultState : { ValveState::CLOSED, ValveState::NONE, ValveState::OPEN }) {
        // Open when first schedule starts, and keep open
        EXPECT_EQ(scheduler.getStateUpdate(schedules, base + 5min, defaultState), (ValveStateUpdate { ValveState::OPEN, 15min }));
        EXPECT_EQ(scheduler.getStateUpdate(schedules, base + 5min + 1s, defaultState), (ValveStateUpdate { ValveState::OPEN, 15min - 1s }));
        EXPECT_EQ(scheduler.getStateUpdate(schedules, base + 10min, defaultState), (ValveStateUpdate { ValveState::OPEN, 15min }));
        EXPECT_EQ(scheduler.getStateUpdate(schedules, base + 15min, defaultState), (ValveStateUpdate { ValveState::OPEN, 10min }));
        EXPECT_EQ(scheduler.getStateUpdate(schedules, base + 25min - 1s, defaultState), (ValveStateUpdate { ValveState::OPEN, 1s }));

        // Close again after later schedule ends, and reopen when first schedule starts again
        EXPECT_EQ(scheduler.getStateUpdate(schedules, base + 25min, defaultState), (ValveStateUpdate { ValveState::CLOSED, 40min }));
        EXPECT_EQ(scheduler.getStateUpdate(schedules, base + 25min + 1s, defaultState), (ValveStateUpdate { ValveState::CLOSED, 40min - 1s }));
    }
}

}    // namespace farmhub::peripherals::valve
