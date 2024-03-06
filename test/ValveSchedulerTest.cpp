#include <gtest/gtest.h>

// TODO Move this someplace else?
#define configTICK_RATE_HZ 1000

#include <ctime>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <time.h>

#include <ArduinoJson.h>

#include <peripherals/valve/ValveScheduler.hpp>

using namespace std::chrono;
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
    ValveSchedule schedule(base, hours(1), minutes(1));
    EXPECT_EQ(schedule.getStart(), base);
    EXPECT_EQ(schedule.getPeriod(), hours(1));
    EXPECT_EQ(schedule.getDuration(), minutes(1));
}

// TEST_F(ValveSchedulerTest, can_create_schedule_from_json) {
//     JsonDocument doc;
//     deserializeJson(doc, R"({
//         "start": "2020-01-01T00:00:00Z",
//         "period": 60,
//         "duration": 15
//     })");
//     ValveSchedule schedule(doc.as<JsonObject>());
//     EXPECT_EQ(schedule.getStart(), time_point<system_clock> { system_clock::from_time_t(1577836800) });
//     EXPECT_EQ(schedule.getPeriod(), minutes(1));
//     EXPECT_EQ(schedule.getDuration(), seconds(15));
// }

TEST_F(ValveSchedulerTest, not_scheduled_when_empty) {
    for (ValveState defaultState : { ValveState::CLOSED, ValveState::NONE, ValveState::OPEN }) {
        ValveStateUpdate update = scheduler.getStateUpdate({}, base, defaultState);
        EXPECT_EQ(update, (ValveStateUpdate { defaultState, ticks::max() }));
    }
}

TEST_F(ValveSchedulerTest, keeps_closed_until_schedule_starts) {
    std::list<ValveSchedule> schedules {
        ValveSchedule(base, hours(1), seconds(15)),
    };
    for (ValveState defaultState : { ValveState::CLOSED, ValveState::NONE, ValveState::OPEN }) {
        EXPECT_EQ(scheduler.getStateUpdate(schedules, base - seconds(1), defaultState), (ValveStateUpdate { ValveState::CLOSED, seconds(1) }));
    }
}

TEST_F(ValveSchedulerTest, keeps_open_when_schedule_is_started_and_in_period) {
    std::list<ValveSchedule> schedules {
        ValveSchedule(base, hours(1), seconds(15)),
    };
    for (ValveState defaultState : { ValveState::CLOSED, ValveState::NONE, ValveState::OPEN }) {
        EXPECT_EQ(scheduler.getStateUpdate(schedules, base, defaultState), (ValveStateUpdate { ValveState::OPEN, seconds(15) }));
        EXPECT_EQ(scheduler.getStateUpdate(schedules, base + seconds(1), defaultState), (ValveStateUpdate { ValveState::OPEN, seconds(14) }));
    }
}

TEST_F(ValveSchedulerTest, keeps_closed_when_schedule_is_started_and_outside_period) {
    std::list<ValveSchedule> schedules {
        ValveSchedule(base, hours(1), seconds(15)),
    };
    for (ValveState defaultState : { ValveState::CLOSED, ValveState::NONE, ValveState::OPEN }) {
        EXPECT_EQ(scheduler.getStateUpdate(schedules, base + seconds(15), defaultState), (ValveStateUpdate { ValveState::CLOSED, hours(1) - seconds(15) }));
        EXPECT_EQ(scheduler.getStateUpdate(schedules, base + seconds(16), defaultState), (ValveStateUpdate { ValveState::CLOSED, hours(1) - seconds(16) }));
    }
}

TEST_F(ValveSchedulerTest, when_there_are_overlapping_schedules_keep_closed_until_earliest_opens) {
    // --OOOOOO--------------
    // ----OOOOOO------------
    std::list<ValveSchedule> schedules {
        ValveSchedule(base + minutes(5), hours(1), minutes(15)),
        ValveSchedule(base + minutes(10), hours(1), minutes(15)),
    };
    for (ValveState defaultState : { ValveState::CLOSED, ValveState::NONE, ValveState::OPEN }) {
        // Keep closed until first schedule starts
        EXPECT_EQ(scheduler.getStateUpdate(schedules, base, defaultState), (ValveStateUpdate { ValveState::CLOSED, minutes(5) }));
        EXPECT_EQ(scheduler.getStateUpdate(schedules, base + seconds(1), defaultState), (ValveStateUpdate { ValveState::CLOSED, minutes(5) - seconds(1) }));
    }
}


TEST_F(ValveSchedulerTest, when_there_are_overlapping_schedules_keep_open_until_latest_closes) {
    // --OOOOOO--------------
    // ----OOOOOO------------
    std::list<ValveSchedule> schedules {
        ValveSchedule(base + minutes(5), hours(1), minutes(15)),
        ValveSchedule(base + minutes(10), hours(1), minutes(15)),
    };
    for (ValveState defaultState : { ValveState::CLOSED, ValveState::NONE, ValveState::OPEN }) {
        // Open when first schedule starts, and keep open
        EXPECT_EQ(scheduler.getStateUpdate(schedules, base + minutes(5), defaultState), (ValveStateUpdate { ValveState::OPEN, minutes(15) }));
        EXPECT_EQ(scheduler.getStateUpdate(schedules, base + minutes(5) + seconds(1), defaultState), (ValveStateUpdate { ValveState::OPEN, minutes(15) - seconds(1) }));
        EXPECT_EQ(scheduler.getStateUpdate(schedules, base + minutes(10), defaultState), (ValveStateUpdate { ValveState::OPEN, minutes(15) }));
        EXPECT_EQ(scheduler.getStateUpdate(schedules, base + minutes(15), defaultState), (ValveStateUpdate { ValveState::OPEN, minutes(10) }));
        EXPECT_EQ(scheduler.getStateUpdate(schedules, base + minutes(25) - seconds(1), defaultState), (ValveStateUpdate { ValveState::OPEN, seconds(1) }));

        // Close again after later schedule ends, and reopen when first schedule starts again
        EXPECT_EQ(scheduler.getStateUpdate(schedules, base + minutes(25), defaultState), (ValveStateUpdate { ValveState::CLOSED, minutes(40) }));
        EXPECT_EQ(scheduler.getStateUpdate(schedules, base + minutes(25) + seconds(1), defaultState), (ValveStateUpdate { ValveState::CLOSED, minutes(40) - seconds(1) }));
    }
}

}    // namespace farmhub::peripherals::valve
