#include <gtest/gtest.h>

#include <chrono>
#include <iostream>
#include <sstream>
#include <stdexcept>

#include <ArduinoJson.h>

#include <peripherals/valve/ValveSchedule.hpp>

using namespace std::chrono;
using namespace std::chrono_literals;

namespace farmhub::peripherals::valve {

class ValveScheduleTest : public testing::Test {
public:
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
};

TEST_F(ValveScheduleTest, can_parse_schedule) {
    char json[] = R"({
        "start": "2024-01-01T00:00:00Z",
        "period": 3600,
        "duration": 900
    })";
    ValveSchedule schedule = fromJson(json);
    EXPECT_EQ(schedule.getStart(), system_clock::from_time_t(1704067200));
    EXPECT_EQ(schedule.getPeriod(), 1h);
    EXPECT_EQ(schedule.getDuration(), 15min);
}

TEST_F(ValveScheduleTest, can_serialize_schedule) {
    ValveSchedule schedule {
        system_clock::from_time_t(1704067200),
        1h,
        15min
    };
    std::string json = toJson(schedule);
    EXPECT_EQ(json, R"({"start":"2024-01-01T00:00:00Z","period":3600,"duration":900})");
}

}    // namespace farmhub::peripherals::valve
