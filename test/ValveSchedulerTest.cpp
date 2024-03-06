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
    ValveSchedule schedule(base, hours { 1 }, minutes { 1 });
    EXPECT_EQ(schedule.getStart(), base);
    EXPECT_EQ(schedule.getPeriod(), hours { 1 });
    EXPECT_EQ(schedule.getDuration(), minutes { 1 });
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
//     EXPECT_EQ(schedule.getPeriod(), minutes { 1 });
//     EXPECT_EQ(schedule.getDuration(), seconds { 15 });
// }

TEST_F(ValveSchedulerTest, not_scheduled_when_empty) {
    for (ValveState defaultState : { ValveState::CLOSED, ValveState::NONE, ValveState::OPEN }) {
        ValveStateUpdate update = scheduler.getStateUpdate({}, base, defaultState);
        EXPECT_EQ(update, (ValveStateUpdate { defaultState, ticks::max() }));
    }
}

TEST_F(ValveSchedulerTest, keeps_closed_until_schedule_starts) {
    std::list<ValveSchedule> schedules {
        ValveSchedule(base, hours { 1 }, seconds { 15 }),
    };
    for (ValveState defaultState : { ValveState::CLOSED, ValveState::NONE, ValveState::OPEN }) {
        EXPECT_EQ(scheduler.getStateUpdate(schedules, base - seconds { 1 }, defaultState), (ValveStateUpdate { ValveState::CLOSED, seconds { 1 } }));
    }
}

TEST_F(ValveSchedulerTest, keeps_open_when_schedule_is_started) {
    std::list<ValveSchedule> schedules {
        ValveSchedule(base, hours { 1 }, seconds { 15 }),
    };
    for (ValveState defaultState : { ValveState::CLOSED, ValveState::NONE, ValveState::OPEN }) {
        EXPECT_EQ(scheduler.getStateUpdate(schedules, base, defaultState), (ValveStateUpdate { ValveState::OPEN, seconds { 15 } }));
        EXPECT_EQ(scheduler.getStateUpdate(schedules, base + seconds { 1 }, defaultState), (ValveStateUpdate { ValveState::OPEN, seconds { 14 } }));
    }
    // EXPECT_TRUE(scheduler.isScheduled(schedules, base));
    // EXPECT_TRUE(scheduler.isScheduled(schedules, base + seconds { 1 }));
    // EXPECT_TRUE(scheduler.isScheduled(schedules, base + seconds { 14 }));
    // EXPECT_FALSE(scheduler.isScheduled(schedules, base + seconds { 15 }));
    // EXPECT_FALSE(scheduler.isScheduled(schedules, base + seconds { 30 }));
    // EXPECT_FALSE(scheduler.isScheduled(schedules, base + seconds { 59 }));
    // EXPECT_TRUE(scheduler.isScheduled(schedules, base + seconds { 60 }));
    // EXPECT_TRUE(scheduler.isScheduled(schedules, base + seconds { 74 }));
    // EXPECT_FALSE(scheduler.isScheduled(schedules, base + seconds { 75 }));
}

// TEST_F(ValveSchedulerTest, does_not_match_schedule_not_yet_started) {
//     std::list<ValveSchedule> schedules {
//         ValveSchedule(base, minutes { 1 }, minutes { 1 }),
//     };
//     EXPECT_FALSE(scheduler.isScheduled(schedules, base - seconds { 1 }));
//     EXPECT_TRUE(scheduler.isScheduled(schedules, base));
// }

// TEST_F(ValveSchedulerTest, matches_multiple_schedules) {
//     std::list<ValveSchedule> schedules {
//         ValveSchedule(base, minutes { 1 }, seconds { 15 }),
//         ValveSchedule(base, minutes { 5 }, seconds { 60 }),
//     };
//     EXPECT_TRUE(scheduler.isScheduled(schedules, base));
//     EXPECT_TRUE(scheduler.isScheduled(schedules, base + seconds { 1 }));
//     EXPECT_TRUE(scheduler.isScheduled(schedules, base + seconds { 14 }));
//     EXPECT_TRUE(scheduler.isScheduled(schedules, base + seconds { 15 }));
//     EXPECT_TRUE(scheduler.isScheduled(schedules, base + seconds { 30 }));
//     EXPECT_TRUE(scheduler.isScheduled(schedules, base + seconds { 59 }));
//     EXPECT_TRUE(scheduler.isScheduled(schedules, base + seconds { 60 }));
//     EXPECT_TRUE(scheduler.isScheduled(schedules, base + seconds { 74 }));
//     EXPECT_FALSE(scheduler.isScheduled(schedules, base + seconds { 75 }));

//     EXPECT_TRUE(scheduler.isScheduled(schedules, base + minutes { 2 }));
//     EXPECT_TRUE(scheduler.isScheduled(schedules, base + minutes { 2 } + seconds { 1 }));
//     EXPECT_TRUE(scheduler.isScheduled(schedules, base + minutes { 2 } + seconds { 14 }));
//     EXPECT_FALSE(scheduler.isScheduled(schedules, base + minutes { 2 } + seconds { 15 }));
//     EXPECT_FALSE(scheduler.isScheduled(schedules, base + minutes { 2 } + seconds { 30 }));
//     EXPECT_FALSE(scheduler.isScheduled(schedules, base + minutes { 2 } + seconds { 59 }));
//     EXPECT_TRUE(scheduler.isScheduled(schedules, base + minutes { 2 } + seconds { 60 }));
//     EXPECT_TRUE(scheduler.isScheduled(schedules, base + minutes { 2 } + seconds { 74 }));
//     EXPECT_FALSE(scheduler.isScheduled(schedules, base + minutes { 2 } + seconds { 75 }));
// }

}    // namespace farmhub::peripherals::valve
