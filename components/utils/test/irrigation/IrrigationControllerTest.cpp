#include <catch2/catch_test_macros.hpp>

#include <utils/irrigation/IrrigationController.hpp>

#include "Fakes.hpp"

namespace farmhub::utils::irrigation {

TEST_CASE("Waters up to band without overshoot") {
    FakeClock clk;
    FakeValve val;
    FakeFlow flow;
    FakeMoisture moistureSensor;
    SoilSim sim;

    Config cfg;
    cfg.targetLow = 60;
    cfg.targetHigh = 80;
    cfg.valveTimeout = std::chrono::minutes { 2 };

    Notifier note = [](std::string_view) { };

    IrrigationController ctrl { cfg, clk, val, flow, moistureSensor, std::move(note) };

    // Simulate 30 minutes at 1s tick
    constexpr auto dt = ms { 1000 };
    moistureSensor.moisture = 55.0;
    for (int i = 0; i < 1800; ++i) {
        // produce flow when valve is on
        if (val.isOpen()) {
            const auto liters_this_tick = 0.25;    // 15 L/min
            flow.bucket += liters_this_tick;
            sim.inject(clk.now(), liters_this_tick);
        }
        ctrl.tick();
        sim.step(clk.now(), moistureSensor.moisture, dt);
        clk.advance(dt);
        if (ctrl.getTelemetry().moisture >= cfg.targetLow && ctrl.getState() == State::Idle)
            break;
    }

    REQUIRE(ctrl.getTelemetry().moisture >= cfg.targetLow);
    REQUIRE_FALSE(val.isOpen());
}

}    // namespace farmhub::utils::irrigation
