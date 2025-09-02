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

static constexpr SoilSimulator::Config BASIC_SOIL = {
    .gainPercentPerLiter = 1.0,
    .deadTime = 20s,
    .tau = 40s,
    .evaporationPercentPerMin = 0.05,
};

struct SimulationConfig {
    Percent startMoisture = 55.0;
    seconds timeout = 30min;
    seconds defaultTick = 5s;
    Liters flowRatePerMinute = 15.0;
};

struct SimulationResult {
    Percent moisture;
    TargetState state;
    ms time;
    int steps;
};

SimulationResult simulate(SoilSimulator::Config soilConfig, Config config, SimulationConfig simulationConfig) {
    FakeClock clock;
    auto flowMeter = std::make_shared<FakeFlowMeter>();
    auto moistureSensor = std::make_shared<FakeSoilMoistureSensor>();
    MoistureBasedScheduler scheduler { config, clock, flowMeter, moistureSensor };
    SoilSimulator soil { soilConfig };

    moistureSensor->moisture = simulationConfig.startMoisture;

    ScheduleResult result;
    int steps = 0;
    while (clock.now() < simulationConfig.timeout) {
        result = scheduler.tick();
        if (scheduler.getState() == State::Idle) {
            LOGV("Reached idle state with moisture level: %f", scheduler.getTelemetry().moisture);
            break;
        }

        auto tick = result.nextDeadline.value_or(simulationConfig.defaultTick);
        // Produce flow when valve is on
        if (result.targetState == TargetState::OPEN) {
            const Liters volumePerTick = simulationConfig.flowRatePerMinute * chrono_ratio(tick, 1min);
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
        steps++;
    }

    LOGV("Final moisture level: %f after %lld sec, %d steps",
        scheduler.getTelemetry().moisture,
        duration_cast<seconds>(clock.now()).count(),
        steps);

    return {
        .moisture = scheduler.getTelemetry().moisture,
        .state = result.targetState.value_or(TargetState::CLOSED),
        .time = clock.now(),
        .steps = steps,
    };
}

TEST_CASE("waters up to band without overshoot") {
    auto result = simulate(
        BASIC_SOIL,
        {
            .targetLow = 60,
            .targetHigh = 70,
            .valveTimeout = std::chrono::minutes { 2 },
        },
        {
            .startMoisture = 55.0,
            .flowRatePerMinute = 15.0,
        });

    REQUIRE(result.state == TargetState::CLOSED);
    REQUIRE(result.time < 15min);
    REQUIRE(result.steps < 80);
    REQUIRE(result.moisture >= 60);
    REQUIRE(result.moisture <= 70);
}

}    // namespace farmhub::utils::scheduling
