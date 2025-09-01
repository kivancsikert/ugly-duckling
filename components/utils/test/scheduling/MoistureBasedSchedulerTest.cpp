#include <catch2/catch_test_macros.hpp>

#include <utils/scheduling/MoistureBasedScheduler.hpp>

#include "Fakes.hpp"

#include <utils/Chrono.hpp>

namespace farmhub::utils::scheduling {

constexpr auto oneTick = 1000ms;

void log(std::string_view message) {
    printf("Moisture based scheduler: %s\n", message.data());
}

TEST_CASE("Waters up to band without overshoot") {
    FakeClock clock;
    auto valve = std::make_shared<FakeValve>();
    auto flowMeter = std::make_shared<FakeFlowMeter>();
    auto moistureSensor = std::make_shared<FakeSoilMoistureSensor>();
    SoilSimulator soil;

    Config config = {
        .targetLow = 60,
        .targetHigh = 80,
        .valveTimeout = std::chrono::minutes { 2 },
    };

    MoistureBasedScheduler scheduler { config, clock, valve, flowMeter, moistureSensor, log };

    // Simulate 30 minutes at 1s tick
    moistureSensor->moisture = 55.0;
    for (int i = 0; i < 1800; ++i) {
        // Produce flow when valve is on
        if (valve->isOpen()) {
            constexpr Liters flowRatePerMinute = 15.0; // L / min
            const Liters volumePerTick = flowRatePerMinute * chrono_ratio(oneTick, 1min);
            flowMeter->bucket += volumePerTick;
            soil.inject(clock.now(), volumePerTick);
        }
        scheduler.tick();
        soil.step(clock.now(), moistureSensor->moisture, oneTick);
        clock.advance(oneTick);
        if (scheduler.getTelemetry().moisture >= config.targetLow && scheduler.getState() == State::Idle) {
            break;
        }
    }

    REQUIRE(scheduler.getTelemetry().moisture >= config.targetLow);
    REQUIRE(valve->isOpen() == false);
}

}    // namespace farmhub::utils::scheduling
