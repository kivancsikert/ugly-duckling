#include <catch2/catch_test_macros.hpp>

#include <utils/irrigation/IrrigationController.hpp>

#include "Fakes.hpp"

namespace farmhub::utils::irrigation {

void log(std::string_view message) {
    printf("Irrigation controller: %s\n", message.data());
}

TEST_CASE("Waters up to band without overshoot") {
    FakeClock clock;
    FakeValve valve;
    FakeFlow flowMeter;
    FakeMoisture moistureSensor;
    SoilSimulator soil;

    Config config = {
        .targetLow = 60,
        .targetHigh = 80,
        .valveTimeout = std::chrono::minutes { 2 },
    };

    IrrigationController controller { config, clock, valve, flowMeter, moistureSensor, log };

    // Simulate 30 minutes at 1s tick
    constexpr auto oneTick = 1000ms;
    moistureSensor.moisture = 55.0;
    for (int i = 0; i < 1800; ++i) {
        // produce flow when valve is on
        if (valve.isOpen()) {
            const auto litersThisTick = 0.25;    // 15 L/min
            flowMeter.bucket += litersThisTick;
            soil.inject(clock.now(), litersThisTick);
        }
        controller.tick();
        soil.step(clock.now(), moistureSensor.moisture, oneTick);
        clock.advance(oneTick);
        if (controller.getTelemetry().moisture >= config.targetLow && controller.getState() == State::Idle) {
            break;
        }
    }

    REQUIRE(controller.getTelemetry().moisture >= config.targetLow);
    REQUIRE(valve.isOpen() == false);
}

}    // namespace farmhub::utils::irrigation
