#include <gtest/gtest.h>

#include <peripherals/ValveScheduler.hpp>

using std::chrono::hours;
using std::chrono::minutes;
using std::chrono::seconds;
using std::chrono::system_clock;
using std::chrono::time_point;

using namespace farmhub::peripherals;

class ValveSchedulerTest : public ::testing::Test {
public:
    ValveSchedulerTest() = default;

    const time_point<system_clock> base { system_clock::now() };
    ValveScheduler scheduler;
};

TEST_F(ValveSchedulerTest, can_create_schedule) {
    ValveSchedule schedule(base, hours { 1 }, minutes { 1 });
    EXPECT_EQ(schedule.start, base);
    EXPECT_EQ(schedule.period, hours { 1 });
    EXPECT_EQ(schedule.duration, minutes { 1 });
}

TEST_F(ValveSchedulerTest, can_create_schedule_from_string) {
    ValveSchedule schedule("2020-01-01T00:00:00Z", hours { 1 }, minutes { 1 });
    EXPECT_EQ(schedule.start, time_point<system_clock> { system_clock::from_time_t(1577836800) });
    EXPECT_EQ(schedule.period, hours { 1 });
    EXPECT_EQ(schedule.duration, minutes { 1 });
}

TEST_F(ValveSchedulerTest, can_create_schedule_from_json) {
    DynamicJsonDocument doc(2048);
    deserializeJson(doc, R"({
        "start": "2020-01-01T00:00:00Z",
        "period": 60,
        "duration": 15
    })");
    ValveSchedule schedule(doc.as<JsonObject>());
    EXPECT_EQ(schedule.start, time_point<system_clock> { system_clock::from_time_t(1577836800) });
    EXPECT_EQ(schedule.period, minutes { 1 });
    EXPECT_EQ(schedule.duration, seconds { 15 });
}

TEST_F(ValveSchedulerTest, not_scheduled_when_empty) {
    EXPECT_FALSE(scheduler.isScheduled({}, base));
}

TEST_F(ValveSchedulerTest, matches_single_schedule) {
    std::list<ValveSchedule> schedules {
        ValveSchedule(base, minutes { 1 }, seconds { 15 }),
    };
    EXPECT_TRUE(scheduler.isScheduled(schedules, base));
    EXPECT_TRUE(scheduler.isScheduled(schedules, base + seconds { 1 }));
    EXPECT_TRUE(scheduler.isScheduled(schedules, base + seconds { 14 }));
    EXPECT_FALSE(scheduler.isScheduled(schedules, base + seconds { 15 }));
    EXPECT_FALSE(scheduler.isScheduled(schedules, base + seconds { 30 }));
    EXPECT_FALSE(scheduler.isScheduled(schedules, base + seconds { 59 }));
    EXPECT_TRUE(scheduler.isScheduled(schedules, base + seconds { 60 }));
    EXPECT_TRUE(scheduler.isScheduled(schedules, base + seconds { 74 }));
    EXPECT_FALSE(scheduler.isScheduled(schedules, base + seconds { 75 }));
}

TEST_F(ValveSchedulerTest, does_not_match_schedule_not_yet_started) {
    std::list<ValveSchedule> schedules {
        ValveSchedule(base, minutes { 1 }, minutes { 1 }),
    };
    EXPECT_FALSE(scheduler.isScheduled(schedules, base - seconds { 1 }));
    EXPECT_TRUE(scheduler.isScheduled(schedules, base));
}

TEST_F(ValveSchedulerTest, matches_multiple_schedules) {
    std::list<ValveSchedule> schedules {
        ValveSchedule(base, minutes { 1 }, seconds { 15 }),
        ValveSchedule(base, minutes { 5 }, seconds { 60 }),
    };
    EXPECT_TRUE(scheduler.isScheduled(schedules, base));
    EXPECT_TRUE(scheduler.isScheduled(schedules, base + seconds { 1 }));
    EXPECT_TRUE(scheduler.isScheduled(schedules, base + seconds { 14 }));
    EXPECT_TRUE(scheduler.isScheduled(schedules, base + seconds { 15 }));
    EXPECT_TRUE(scheduler.isScheduled(schedules, base + seconds { 30 }));
    EXPECT_TRUE(scheduler.isScheduled(schedules, base + seconds { 59 }));
    EXPECT_TRUE(scheduler.isScheduled(schedules, base + seconds { 60 }));
    EXPECT_TRUE(scheduler.isScheduled(schedules, base + seconds { 74 }));
    EXPECT_FALSE(scheduler.isScheduled(schedules, base + seconds { 75 }));

    EXPECT_TRUE(scheduler.isScheduled(schedules, base + minutes { 2 }));
    EXPECT_TRUE(scheduler.isScheduled(schedules, base + minutes { 2 } + seconds { 1 }));
    EXPECT_TRUE(scheduler.isScheduled(schedules, base + minutes { 2 } + seconds { 14 }));
    EXPECT_FALSE(scheduler.isScheduled(schedules, base + minutes { 2 } + seconds { 15 }));
    EXPECT_FALSE(scheduler.isScheduled(schedules, base + minutes { 2 } + seconds { 30 }));
    EXPECT_FALSE(scheduler.isScheduled(schedules, base + minutes { 2 } + seconds { 59 }));
    EXPECT_TRUE(scheduler.isScheduled(schedules, base + minutes { 2 } + seconds { 60 }));
    EXPECT_TRUE(scheduler.isScheduled(schedules, base + minutes { 2 } + seconds { 74 }));
    EXPECT_FALSE(scheduler.isScheduled(schedules, base + minutes { 2 } + seconds { 75 }));
}
