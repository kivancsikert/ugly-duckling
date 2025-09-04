#include <functional>

#include <catch2/catch_test_macros.hpp>

#include <FakeLog.hpp>

#include <utils/scheduling/MoistureBasedScheduler.hpp>

#include "Fakes.hpp"

#include <utils/Chrono.hpp>

namespace farmhub::utils::scheduling {

static constexpr SoilSimulator::Config BASIC_SOIL = {
    .gainPercentPerLiter = 1.0,
    .deadTime = 20s,
    .tau = 40s,
    .evaporationPercentPerMin = 0.05,
};

using SchedulerRef = MoistureBasedScheduler<FakeClock> const&;

struct SimulationConfig {
    seconds timeout = 30min;
    seconds defaultTick = 5s;
    std::function<bool(SchedulerRef)> stopCondition = [](SchedulerRef scheduler) {
        if (scheduler.getState() == State::Idle) {
            LOGTV(TEST, "Reached idle state with moisture level: %f", scheduler.getTelemetry().moisture);
            return true;
        }
        return false;
    };

    Percent startMoisture = 55.0;
    Liters flowRatePerMinute = 15.0;
};

struct SimulationResult {
    ms time;
    int steps;
    Percent moisture;
    std::optional<TargetState> targetState;

    bool operator==(const SimulationResult& other) const {
        return time == other.time
            && steps == other.steps
            && moisture == other.moisture
            && targetState == other.targetState;
    }
};

SimulationResult simulate(SoilSimulator::Config soilConfig, Config config, std::optional<MoistureTarget> target, SimulationConfig simulationConfig) {
    auto clock = std::make_shared<FakeClock>();
    auto flowMeter = std::make_shared<FakeFlowMeter>();
    auto moistureSensor = std::make_shared<FakeSoilMoistureSensor>();
    MoistureBasedScheduler scheduler { config, clock, flowMeter, moistureSensor };
    scheduler.setTarget(target);
    SoilSimulator soil { soilConfig };

    moistureSensor->moisture = simulationConfig.startMoisture;

    ScheduleResult result;
    int steps = 0;
    while (clock->now() < simulationConfig.timeout) {
        result = scheduler.tick();
        steps++;
        auto tick = result.nextDeadline.value_or(simulationConfig.defaultTick);
        LOGTV(TEST, "At %lld sec in %s state, valve is %s, moisture level is %f%%, advancing by %lld sec",
            duration_cast<seconds>(clock->now()).count(),
            toString(scheduler.getState()),
            toString(result.targetState),
            scheduler.getTelemetry().moisture,
            duration_cast<seconds>(tick).count());

        if (simulationConfig.stopCondition(scheduler)) {
            break;
        }

        // Produce flow when valve is on
        if (result.targetState == TargetState::Open) {
            const Liters volumePerTick = simulationConfig.flowRatePerMinute * chrono_ratio(tick, 1min);
            LOGTV(TEST, "Injecting %f liters of water", volumePerTick);
            flowMeter->bucket += volumePerTick;
            soil.inject(clock->now(), volumePerTick);
        }

        soil.step(clock->now(), moistureSensor->moisture, tick);

        clock->advance(tick);
    }

    LOGTV(TEST, "Final moisture level: %f after %lld sec, %d steps",
        scheduler.getTelemetry().moisture,
        duration_cast<seconds>(clock->now()).count(),
        steps);

    return {
        .time = clock->now(),
        .steps = steps,
        .moisture = scheduler.getTelemetry().moisture,
        .targetState = result.targetState,
    };
}

SimulationResult simulate(SoilSimulator::Config soilConfig, MoistureTarget target, SimulationConfig simulationConfig) {
    return simulate(soilConfig, {}, target, simulationConfig);
}

TEST_CASE("does not water when there is no target specified") {
    auto result = simulate(
        BASIC_SOIL,
        {},
        {},
        {
            .startMoisture = 10.0,
        });

    REQUIRE(result.time == 0ms);
    REQUIRE(result.steps == 1);
    REQUIRE(result.targetState.has_value() == false);
}

TEST_CASE("does not water when moisture is already above target") {
    auto result = simulate(
        BASIC_SOIL,
        {
            .low = 60,
            .high = 70,
        },
        {
            .startMoisture = 65.0,
        });

    REQUIRE(result == SimulationResult {
                .time = 0ms,
                .steps = 1,
                .moisture = 65.0,
                .targetState = TargetState::Closed,
            });
}

TEST_CASE("waters up to band without overshoot") {
    auto result = simulate(
        BASIC_SOIL,
        {
            .low = 60,
            .high = 70,
        },
        {
            .startMoisture = 55.0,
            .flowRatePerMinute = 15.0,
        });

    REQUIRE(result.targetState == TargetState::Closed);
    REQUIRE(result.time < 15min);
    REQUIRE(result.steps < 80);
    REQUIRE(result.moisture >= 60.0);
    REQUIRE(result.moisture <= 70.0);
}

TEST_CASE("starts watering after evaporation reduces moisture") {
    auto result = simulate(
        BASIC_SOIL,
        {
            .low = 60,
            .high = 70,
        },
        {
            .stopCondition = [](SchedulerRef scheduler) {
                return scheduler.getState() != State::Idle;
            },
            .startMoisture = 61.0,
            .flowRatePerMinute = 15.0,
        });

    REQUIRE(result.targetState == TargetState::Open);
    REQUIRE(result.steps > 10);
    REQUIRE(result.moisture < 60.0);
    REQUIRE(result.moisture > 59.0);
}

}    // namespace farmhub::utils::scheduling

namespace Catch {

using farmhub::utils::scheduling::SimulationResult;

template <>
struct StringMaker<SimulationResult> {
    static std::string convert(SimulationResult const& s) {
        std::ostringstream oss;
        oss << "SimulationResult{time=" << s.time.count()
            << "ms, steps=" << s.steps
            << ", moisture=" << s.moisture
            << "%, targetState=" << toString(s.targetState) << "}";
        return oss.str();
    }
};

}    // namespace Catch
