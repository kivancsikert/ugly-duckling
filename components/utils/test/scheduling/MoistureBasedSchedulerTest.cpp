#include <catch2/catch_test_macros.hpp>

#include <FakeLog.hpp>

#include <utils/scheduling/MoistureBasedScheduler.hpp>

#include "Fakes.hpp"

#include <utils/Chrono.hpp>

namespace farmhub::utils::scheduling {

inline const char* toString(State state) {
    switch (state) {
        case State::Idle:
            return "idle";
        case State::Watering:
            return "watering";
        case State::Soak:
            return "soak";
        case State::UpdateModel:
            return "update model";
        case State::Fault:
            return "fault";
        default:
            return "unknown";
    }
}

TEST_CASE("Waters up to band without overshoot") {
    FakeClock clock;
    auto flowMeter = std::make_shared<FakeFlowMeter>();
    auto moistureSensor = std::make_shared<FakeSoilMoistureSensor>();
    SoilSimulator soil;

    Config config = {
        .targetLow = 60,
        .targetHigh = 70,
        .valveTimeout = std::chrono::minutes { 2 },
    };

    MoistureBasedScheduler scheduler { config, clock, flowMeter, moistureSensor };

    moistureSensor->moisture = 55.0;
    ScheduleResult result;
    while (clock.now() < 1h) {
        if (scheduler.getTelemetry().moisture >= config.targetLow && scheduler.getState() == State::Idle) {
            break;
        }

        result = scheduler.tick();
        auto tick = result.nextDeadline.value_or(5s);

        // Produce flow when valve is on
        if (result.targetState == TargetState::OPEN) {
            constexpr Liters flowRatePerMinute = 15.0;    // L / min
            const Liters volumePerTick = flowRatePerMinute * chrono_ratio(tick, 1min);
            LOGV("Injecting %f liters of water", volumePerTick);
            flowMeter->bucket += volumePerTick;
            soil.inject(clock.now(), volumePerTick);
        }

        soil.step(clock.now(), moistureSensor->moisture, tick);

        LOGV("At %lld sec in %s state, valve is %s, moisture level is %f%%, advancing by %lld sec",
            duration_cast<seconds>(clock.now()).count(),
            toString(scheduler.getState()),
            toString(result.targetState),
            scheduler.getTelemetry().moisture,
            duration_cast<seconds>(tick).count());

        clock.advance(tick);
    }
    LOGV("Final moisture level: %f after %lld sec",
        scheduler.getTelemetry().moisture,
        duration_cast<seconds>(clock.now()).count());

    REQUIRE(scheduler.getTelemetry().moisture >= config.targetLow);
    REQUIRE(result.targetState == TargetState::CLOSED);
}

}    // namespace farmhub::utils::scheduling
