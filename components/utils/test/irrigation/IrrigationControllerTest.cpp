#include <catch2/catch_test_macros.hpp>

#include <utils/irrigation/IrrigationController.hpp>

#include "Fakes.hpp"

namespace farmhub::utils::irrigation {

TEST_CASE("Waters up to band without overshoot") {
    FakeClock clk;
    FakeValve val;
    FakeFlow flow;
    FakeMoisture moist;
    SoilSim sim;

    Config cfg;
    cfg.target_low = 60;
    cfg.target_high = 80;
    cfg.valve_timeout = std::chrono::minutes { 2 };

    Notifier note = [](std::string_view) { };

    Controller ctrl { cfg, clk, val, flow, moist, std::move(note) };

    // Simulate 30 minutes at 1s tick
    constexpr auto dt = ms { 1000 };
    moist.m = 55.f;
    for (int i = 0; i < 1800; ++i) {
        // produce flow when valve is on
        if (val.is_on()) {
            const auto liters_this_tick = 0.25f;    // 15 L/min
            flow.bucket += liters_this_tick;
            sim.inject(clk.now(), liters_this_tick);
        }
        ctrl.tick();
        sim.step(clk.now(), moist.m, dt);
        clk.advance(dt);
        if (ctrl.tel().m >= cfg.target_low && ctrl.state() == State::Idle)
            break;
    }

    REQUIRE(ctrl.tel().m >= cfg.target_low);
    REQUIRE_FALSE(val.is_on());
}

}    // namespace farmhub::utils::irrigation
