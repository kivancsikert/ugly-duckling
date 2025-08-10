#include <catch2/catch_test_macros.hpp>

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

using namespace farmhub::peripherals::valve;

static time_point<system_clock> parseTime(const char* str) {
    tm time;
    std::istringstream ss(str);
    ss >> std::get_time(&time, "%Y-%m-%d %H:%M:%S");
    return system_clock::from_time_t(mktime(&time));
}

namespace farmhub::peripherals::valve {

std::ostream& operator<<(std::ostream& os, ValveState const& val) {
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

template <typename _Rep, typename _Period>
std::ostream& operator<<(std::ostream& os, std::chrono::duration<_Rep, _Period> const& val) {
    os << duration_cast<milliseconds>(val).count() << " ms";
    return os;
}

std::ostream& operator<<(std::ostream& os, ValveStateUpdate const& val) {
    os << "{ state: " << val.state << ", transitionAfter: " << val.validFor << " }";
    return os;
}

}    // namespace farmhub::peripherals::valve

ValveSchedule fromJson(const char* json) {
    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, json);
    if (error) {
        std::ostringstream oss;
        oss << "Cannot parse schedule: " << error.c_str();
        throw std::runtime_error(oss.str());
    }
    return doc.as<ValveSchedule>();
}

std::string toJson(const ValveSchedule& schedule) {
    JsonDocument doc;
    doc.set(schedule);
    std::ostringstream oss;
    serializeJson(doc, oss);
    return oss.str();
}

const time_point<system_clock> T0 = parseTime("2024-01-01 00:00:00");

TEST_CASE("can parse schedule") {
    const char json[] = R"({
        "start": "2024-01-01T00:00:00Z",
        "period": 3600,
        "duration": 900
    })";
    ValveSchedule schedule = fromJson(json);
    REQUIRE(schedule.getStart().time_since_epoch().count() == system_clock::from_time_t(1704067200).time_since_epoch().count());
    REQUIRE(schedule.getPeriod() == 1h);
    REQUIRE(schedule.getDuration() == 15min);
}

TEST_CASE("can serialize schedule") {
    ValveSchedule schedule {
        system_clock::from_time_t(1704067200),
        1h,
        15min
    };
    std::string json = toJson(schedule);
    REQUIRE(json == R"({"start":"2024-01-01T00:00:00Z","period":3600,"duration":900})");
}

TEST_CASE("can create schedule") {
    ValveSchedule schedule(T0, 1h, 1min);
    REQUIRE(schedule.getStart() == T0);
    REQUIRE(schedule.getPeriod() == 1h);
    REQUIRE(schedule.getDuration() == 1min);
}

TEST_CASE("not scheduled when empty") {
    ValveScheduler scheduler;
    for (ValveState defaultState : { ValveState::CLOSED, ValveState::NONE, ValveState::OPEN }) {
    ValveStateUpdate update = scheduler.getStateUpdate({}, T0, defaultState);
        REQUIRE(update == ValveStateUpdate { defaultState, nanoseconds::max() });
    }
}

TEST_CASE("keeps closed until schedule starts") {
    ValveScheduler scheduler;
    std::list<ValveSchedule> schedules {
        ValveSchedule(T0, 1h, 15s),
    };
    for (ValveState defaultState : { ValveState::CLOSED, ValveState::NONE, ValveState::OPEN }) {
        REQUIRE(scheduler.getStateUpdate(schedules, T0 - 1s, defaultState) == ValveStateUpdate { ValveState::CLOSED, 1s });
    }
}

TEST_CASE("keeps open when schedule is started and in period") {
    ValveScheduler scheduler;
    std::list<ValveSchedule> schedules {
        ValveSchedule(T0, 1h, 15s),
    };
    for (ValveState defaultState : { ValveState::CLOSED, ValveState::NONE, ValveState::OPEN }) {
        REQUIRE(scheduler.getStateUpdate(schedules, T0, defaultState) == ValveStateUpdate { ValveState::OPEN, 15s });
        REQUIRE(scheduler.getStateUpdate(schedules, T0 + 1s, defaultState) == ValveStateUpdate { ValveState::OPEN, 14s });
    }
}

TEST_CASE("keeps closed when schedule is started and outside period") {
    ValveScheduler scheduler;
    std::list<ValveSchedule> schedules {
        ValveSchedule(T0, 1h, 15s),
    };
    for (ValveState defaultState : { ValveState::CLOSED, ValveState::NONE, ValveState::OPEN }) {
        REQUIRE(scheduler.getStateUpdate(schedules, T0 + 15s, defaultState) == ValveStateUpdate { ValveState::CLOSED, 1h - 15s });
        REQUIRE(scheduler.getStateUpdate(schedules, T0 + 16s, defaultState) == ValveStateUpdate { ValveState::CLOSED, 1h - 16s });
    }
}

TEST_CASE("when there are overlapping schedules keep closed until earliest opens") {
    ValveScheduler scheduler;
    // --OOOOOO--------------
    // ----OOOOOO------------
    std::list<ValveSchedule> schedules {
        ValveSchedule(T0 + 5min, 1h, 15min),
        ValveSchedule(T0 + 10min, 1h, 15min),
    };
    for (ValveState defaultState : { ValveState::CLOSED, ValveState::NONE, ValveState::OPEN }) {
        // Keep closed until first schedule starts
        REQUIRE(scheduler.getStateUpdate(schedules, T0, defaultState) == ValveStateUpdate { ValveState::CLOSED, 5min });
        REQUIRE(scheduler.getStateUpdate(schedules, T0 + 1s, defaultState) == ValveStateUpdate { ValveState::CLOSED, 5min - 1s });
    }
}

TEST_CASE("when there are overlapping schedules keep open until latest closes") {
    ValveScheduler scheduler;
    // --OOOOOO--------------
    // ----OOOOOO------------
    std::list<ValveSchedule> schedules {
        ValveSchedule(T0 + 5min, 1h, 15min),
        ValveSchedule(T0 + 10min, 1h, 15min),
    };
    for (ValveState defaultState : { ValveState::CLOSED, ValveState::NONE, ValveState::OPEN }) {
        // Open when first schedule starts, and keep open
        REQUIRE(scheduler.getStateUpdate(schedules, T0 + 5min, defaultState) == ValveStateUpdate { ValveState::OPEN, 15min });
        REQUIRE(scheduler.getStateUpdate(schedules, T0 + 5min + 1s, defaultState) == ValveStateUpdate { ValveState::OPEN, 15min - 1s });
        REQUIRE(scheduler.getStateUpdate(schedules, T0 + 10min, defaultState) == ValveStateUpdate { ValveState::OPEN, 15min });
        REQUIRE(scheduler.getStateUpdate(schedules, T0 + 15min, defaultState) == ValveStateUpdate { ValveState::OPEN, 10min });
        REQUIRE(scheduler.getStateUpdate(schedules, T0 + 25min - 1s, defaultState) == ValveStateUpdate { ValveState::OPEN, 1s });

        // Close again after later schedule ends, and reopen when first schedule starts again
        REQUIRE(scheduler.getStateUpdate(schedules, T0 + 25min, defaultState) == ValveStateUpdate { ValveState::CLOSED, 40min });
        REQUIRE(scheduler.getStateUpdate(schedules, T0 + 25min + 1s, defaultState) == ValveStateUpdate { ValveState::CLOSED, 40min - 1s });
    }
}

TEST_CASE("handles back-to-back schedules without gap") {
    ValveScheduler scheduler;
    // Two schedules that touch end-to-start: [0..10s) and [10s..20s)
    std::list<ValveSchedule> schedules{
        ValveSchedule(T0, 30s, 10s),
        ValveSchedule(T0 + 10s, 30s, 10s),
    };

    // At start => OPEN for 10s
    REQUIRE(scheduler.getStateUpdate(schedules, T0, ValveState::CLOSED) == ValveStateUpdate{ValveState::OPEN, 10s});
    // Just before switch => still OPEN
    REQUIRE(scheduler.getStateUpdate(schedules, T0 + 10s - 1ms, ValveState::CLOSED) == ValveStateUpdate{ValveState::OPEN, 1ms});
    // Exactly at boundary => next schedule keeps it OPEN for another 10s
    REQUIRE(scheduler.getStateUpdate(schedules, T0 + 10s, ValveState::CLOSED) == ValveStateUpdate{ValveState::OPEN, 10s});
    // After second ends => CLOSED until next period
    REQUIRE(scheduler.getStateUpdate(schedules, T0 + 20s, ValveState::CLOSED) == ValveStateUpdate{ValveState::CLOSED, 10s});
}

TEST_CASE("stays at default NONE until first open, then reverts correctly") {
    ValveScheduler scheduler;
    std::list<ValveSchedule> schedules{
        ValveSchedule(T0 + 5s, 60s, 2s),
    };

    // Before first start => NONE
    REQUIRE(scheduler.getStateUpdate(schedules, T0, ValveState::NONE) == ValveStateUpdate{ValveState::NONE, 5s});
    // During open => OPEN
    REQUIRE(scheduler.getStateUpdate(schedules, T0 + 5s + 500ms, ValveState::NONE) == ValveStateUpdate{ValveState::OPEN, 1500ms});
    // After close => NONE until next period
    REQUIRE(scheduler.getStateUpdate(schedules, T0 + 7s, ValveState::NONE) == ValveStateUpdate{ValveState::NONE, 58s});
}

TEST_CASE("non-overlapping sequences alternate open and closed as expected") {
    ValveScheduler scheduler;
    std::list<ValveSchedule> schedules{
        ValveSchedule(T0 + 0s,  20s, 5s),
        ValveSchedule(T0 + 10s, 20s, 5s),
    };

    // 0..5s OPEN, 5..10s CLOSED, 10..15s OPEN, 15..20s CLOSED
    REQUIRE(scheduler.getStateUpdate(schedules, T0 + 0s, ValveState::CLOSED) == ValveStateUpdate{ValveState::OPEN, 5s});
    REQUIRE(scheduler.getStateUpdate(schedules, T0 + 5s, ValveState::CLOSED) == ValveStateUpdate{ValveState::CLOSED, 5s});
    REQUIRE(scheduler.getStateUpdate(schedules, T0 + 10s, ValveState::CLOSED) == ValveStateUpdate{ValveState::OPEN, 5s});
    REQUIRE(scheduler.getStateUpdate(schedules, T0 + 15s, ValveState::CLOSED) == ValveStateUpdate{ValveState::CLOSED, 5s});
}
