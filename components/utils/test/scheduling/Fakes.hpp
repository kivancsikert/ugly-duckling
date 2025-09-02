// fakes.hpp
#pragma once

#include <deque>
#include <cmath>

#include <peripherals/api/IFlowMeter.hpp>
#include <peripherals/api/ISoilMoistureSensor.hpp>

#include <utils/scheduling/MoistureBasedScheduler.hpp>

using namespace farmhub::peripherals::api;

namespace farmhub::utils::scheduling {

struct FakeClock {
    ms time { 0 };
    [[nodiscard]] ms now() const {
        return time;
    }
    void advance(ms dt) {
        time += dt;
    }
};

struct FakePeripheral : virtual IPeripheral {

    FakePeripheral(std::string name)
        : name(std::move(name)) {
    }

    const std::string& getName() const override {
        return name;
    }

    const std::string name;
};

struct FakeFlowMeter : FakePeripheral, IFlowMeter {
    FakeFlowMeter()
        : FakePeripheral("flow-meter") {
    }

    Liters bucket { 0.0 };
    Liters getVolume() override {
        auto r = bucket;
        bucket = 0.0;
        return r;
    }
};

struct FakeSoilMoistureSensor : FakePeripheral, ISoilMoistureSensor {
    FakeSoilMoistureSensor()
        : FakePeripheral("soil-moisture-sensor") {
    }

    Percent moisture { 50.0 };
    Percent getMoisture() override {
        return moisture;
    }
};

// Simple FOPDT-ish soil simulator (test-only)
struct SoilSimulator {
    // Parameters (Option B: decaying impulse model)
    // Each watering creates an impulse whose total integrated contribution (area) tends toward gainPercentPerLiter * volume (%).
    double gainPercentPerLiter = 0.25;         // Integrated % moisture per liter (area under exponential after dead time)
    ms deadTime { 10s };                       // Transport delay before water affects moisture
    ms tau { 20s };                            // Decay time constant of the impulse (roughly effect halves every ~0.69*tau)
    double evaporationPercentPerMin = 0.03;    // Natural linear evaporation (% per minute)

    struct Input {
        ms time {};
        Liters volume {};
    };
    std::deque<Input> wateringHistory {};

    void inject(ms now, Liters volume) {
        if (volume > 0.0) {
            wateringHistory.push_back({ now, volume });
        }
        // Trim very old inputs (after effect is negligible: ageAfterDead > 8*tau)
        while (!wateringHistory.empty() && (now - wateringHistory.front().time) > (deadTime + 8 * tau)) {
            wateringHistory.pop_front();
        }
    }

    // Advance one tick (discrete integration of exponential decays)
    void step(ms now, Percent& moisture, ms dt) const {
        if (dt <= 0ms) {
            return; // nothing to do
        }

        // 1. Evaporation (linear approximation)
        const double dtInMin = static_cast<double>(dt.count()) / 60000.0;
        moisture = std::max(0.0, moisture - (evaporationPercentPerMin * dtInMin));

        // 2. Add contributions from each watering whose dead time has passed.
        //    Exact discrete integral over the interval [t, t+dt] of A * (1/tau) * exp(-age/tau) d(age)
        //    where A = gainPercentPerLiter * volume.
        const double tauMs = static_cast<double>(tau.count());
        const double dtMs = static_cast<double>(dt.count());
        const double expNegDtOverTau = std::exp(-dtMs / tauMs);
        double delta = 0.0; // total percent to add this tick
        for (auto const& watering : wateringHistory) {
            auto effectStart = watering.time + deadTime;
            if (now <= effectStart) {
                continue; // not started yet
            }
            // Age at current time (start of interval)
            double ageMs = static_cast<double>((now - effectStart).count());
            // Exponential factor at beginning of interval
            double expNegAge = std::exp(-ageMs / tauMs);
            // Increment = A * (exp(-age/tau) - exp(-(age+dt)/tau)) = A * exp(-age/tau) * (1 - exp(-dt/tau))
            double A = gainPercentPerLiter * watering.volume; // total area (%) this impulse would add over infinite time
            double inc = A * expNegAge * (1.0 - expNegDtOverTau);
            delta += inc;
        }
        moisture = std::min(100.0, moisture + delta);
    }
};

}    // namespace farmhub::utils::scheduling
