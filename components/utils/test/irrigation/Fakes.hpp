// fakes.hpp
#pragma once

#include <deque>

#include <utils/irrigation/IrrigationController.hpp>

namespace farmhub::utils::irrigation {

struct FakeClock {
    ms time { 0 };
    [[nodiscard]] ms now() const {
        return time;
    }
    void advance(ms dt) {
        time += dt;
    }
};

struct FakeValve {
    bool open { false };
    void setState(bool shouldBeOpen) {
        open = shouldBeOpen;
    }
    [[nodiscard]] bool isOpen() const {
        return open;
    }
};

struct FakeFlow {
    Liters bucket { 0.0 };
    Liters getVolume() {
        auto r = bucket;
        bucket = 0.0;
        return r;
    }
};

struct FakeMoisture {
    Percent moisture { 50.0 };
    Percent getMoisture() {
        return moisture;
    }
};

// Simple FOPDT-ish soil simulator (test-only)
struct SoilSimulator {
    // Parameters
    double gainPercentPerLiter = 0.25;              // % / L
    ms deadTime { std::chrono::seconds { 10 } };    // transport delay
    ms tau { std::chrono::seconds { 20 } };         // time constant
    double evaporationPercentPerMin = 0.03;             // natural decay when valve off

    struct Input {
        ms time {};
        Liters volume {};
    };
    std::deque<Input> history {};

    void inject(ms now, Liters volume) {
        if (volume > 0.0) {
            history.push_back({ now, volume });
        }
        // Trim very old inputs
        while (!history.empty() && (now - history.front().time) > (deadTime + 10 * tau)) {
            history.pop_front();
        }
    }

    // Advance one tick
    void step(ms now, Percent& moisture, ms dt) const {
        // Evaporative drift (approximate: linear decay)
        const double dt_min = static_cast<double>(dt.count()) / 60000.0;
        moisture = std::max(0.0, moisture - (evaporationPercentPerMin * dt_min));

        // Aggregate rise from all past inputs after dead-time
        double dm_total = 0.0;
        for (auto const& u : history) {
            if (now <= u.time + deadTime)
                continue;    // not arrived yet
            const double ageInMillis = static_cast<double>((now - (u.time + deadTime)).count());
            const double tauInMillis = static_cast<double>(tau.count());
            const double rise = 1.0 - std::exp(-ageInMillis / tauInMillis);    // 0..1
            dm_total += gainPercentPerLiter * u.volume * rise;       // % contribution
        }

        // Crude discrete application so moisture approaches the simulated target smoothly
        // Scale by dt/tau (bounded) to avoid big jumps on large dt.
        const double scale = std::min(1.0, static_cast<double>(dt.count()) / static_cast<double>(tau.count()));
        moisture = std::min(100.0, moisture + (dm_total * 0.1 * scale));
    }
};

}    // namespace farmhub::utils::irrigation
