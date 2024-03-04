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
using namespace farmhub::peripherals::valve;

static time_point<system_clock> parseTime(const char* str) {
    tm time;
    std::istringstream ss(str);
    ss >> std::get_time(&time, "%Y-%m-%d %H:%M:%S");
    return system_clock::from_time_t(mktime(&time));
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
//     EXPECT_EQ(schedule.start, time_point<system_clock> { system_clock::from_time_t(1577836800) });
//     EXPECT_EQ(schedule.period, minutes { 1 });
//     EXPECT_EQ(schedule.duration, seconds { 15 });
// }

// TEST_F(ValveSchedulerTest, not_scheduled_when_empty) {
//     EXPECT_FALSE(scheduler.isScheduled({}, base));
// }

// TEST_F(ValveSchedulerTest, matches_single_schedule) {
//     std::list<ValveSchedule> schedules {
//         ValveSchedule(base, minutes { 1 }, seconds { 15 }),
//     };
//     EXPECT_TRUE(scheduler.isScheduled(schedules, base));
//     EXPECT_TRUE(scheduler.isScheduled(schedules, base + seconds { 1 }));
//     EXPECT_TRUE(scheduler.isScheduled(schedules, base + seconds { 14 }));
//     EXPECT_FALSE(scheduler.isScheduled(schedules, base + seconds { 15 }));
//     EXPECT_FALSE(scheduler.isScheduled(schedules, base + seconds { 30 }));
//     EXPECT_FALSE(scheduler.isScheduled(schedules, base + seconds { 59 }));
//     EXPECT_TRUE(scheduler.isScheduled(schedules, base + seconds { 60 }));
//     EXPECT_TRUE(scheduler.isScheduled(schedules, base + seconds { 74 }));
//     EXPECT_FALSE(scheduler.isScheduled(schedules, base + seconds { 75 }));
// }

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
