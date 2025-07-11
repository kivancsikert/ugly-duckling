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

std::ostream& operator<<(std::ostream& os, ValveStateDecision const& val) {
    os << "{ state: " << val.state << ", expiresAfter: " << val.expiresAfter << " }";
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

const time_point<system_clock> base = system_clock::from_time_t(1750000000);    // Sun Jun 15 2025 15:06:40 GMT+0000

static ValveStateDecision getStateUpdate(const std::list<ValveSchedule>& schedules, time_point<system_clock> now) {
    std::optional<ValveStateDecision> decision = std::nullopt;
    for (const auto& schedule : schedules) {
        decision = ValveScheduler::updateValveStateDecision(
            decision,
            schedule.getStart(),
            schedule.getDuration(),
            schedule.getPeriod(),
            ValveState::OPEN,
            now);
        if (decision.has_value()) {
            LOGI("-- Interim decision: %s for %lld s",
                decision->state == ValveState::NONE ? "NONE" : (decision->state == ValveState::OPEN ? "OPEN" : "CLOSED"),
                duration_cast<seconds>(decision->expiresAfter).count());
        } else {
            LOGI("-- No interim decision");
        }
    }
    ValveStateDecision finalDecision = decision.value_or(ValveStateDecision { ValveState::NONE, nanoseconds::max() });
    LOGI("-- Final decision: %s for %lld s\n",
        finalDecision.state == ValveState::NONE ? "NONE" : (finalDecision.state == ValveState::OPEN ? "OPEN" : "CLOSED"),
        duration_cast<seconds>(finalDecision.expiresAfter).count());
    return finalDecision;
}

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
    ValveSchedule schedule(base, 1h, 1min);
    REQUIRE(schedule.getStart() == base);
    REQUIRE(schedule.getPeriod() == 1h);
    REQUIRE(schedule.getDuration() == 1min);
}

TEST_CASE("not scheduled when empty") {
    REQUIRE(getStateUpdate({}, base) == ValveStateDecision { ValveState::NONE, nanoseconds::max() });
}

TEST_CASE("requires no state until schedule starts") {
    auto schedules = {
        ValveSchedule(base, 1h, 15s),
    };
    REQUIRE(getStateUpdate(schedules, base - 1s) == ValveStateDecision { ValveState::NONE, 1s });
}

TEST_CASE("keeps open when schedule is started and in period") {
    auto schedules = {
        ValveSchedule(base, 1h, 15s),
    };
    REQUIRE(getStateUpdate(schedules, base) == ValveStateDecision { ValveState::OPEN, 15s });
    REQUIRE(getStateUpdate(schedules, base + 1s) == ValveStateDecision { ValveState::OPEN, 14s });
}

TEST_CASE("requires no state when schedule is started and outside period") {
    auto schedules = {
        ValveSchedule(base, 1h, 15s),
    };
    REQUIRE(getStateUpdate(schedules, base + 15s) == ValveStateDecision { ValveState::NONE, 1h - 15s });
    REQUIRE(getStateUpdate(schedules, base + 16s) == ValveStateDecision { ValveState::NONE, 1h - 16s });
}

TEST_CASE("when there are overlapping schedules require no state until earliest opens") {
    // --OOOOOO--------------
    // ----OOOOOO------------
    auto schedules = {
        ValveSchedule(base + 5min, 1h, 15min),
        ValveSchedule(base + 10min, 1h, 15min),
    };
    REQUIRE(getStateUpdate(schedules, base) == ValveStateDecision { ValveState::NONE, 5min });
    REQUIRE(getStateUpdate(schedules, base + 1s) == ValveStateDecision { ValveState::NONE, 5min - 1s });
}

TEST_CASE("when there are overlapping schedules keep open until latest closes") {
    // --OOOOOO--------------
    // ----OOOOOO------------
    auto schedules = {
        ValveSchedule(base + 5min, 1h, 15min),
        ValveSchedule(base + 10min, 1h, 15min),
    };
    // Open when first schedule starts, and keep open
    REQUIRE(getStateUpdate(schedules, base + 5min) == ValveStateDecision { ValveState::OPEN, 20min });
    REQUIRE(getStateUpdate(schedules, base + 5min + 1s) == ValveStateDecision { ValveState::OPEN, 20min - 1s });
    REQUIRE(getStateUpdate(schedules, base + 10min) == ValveStateDecision { ValveState::OPEN, 15min });
    REQUIRE(getStateUpdate(schedules, base + 15min) == ValveStateDecision { ValveState::OPEN, 10min });
    REQUIRE(getStateUpdate(schedules, base + 25min - 1s) == ValveStateDecision { ValveState::OPEN, 1s });

    // Require no state after later schedule ends, and reopen when first schedule starts again
    REQUIRE(getStateUpdate(schedules, base + 25min) == ValveStateDecision { ValveState::NONE, 40min });
    REQUIRE(getStateUpdate(schedules, base + 25min + 1s) == ValveStateDecision { ValveState::NONE, 40min - 1s });
}
