// fakes.hpp
#pragma once

#include <deque>

#include <utils/irrigation/IrrigationController.hpp>

namespace farmhub::utils::irrigation {

struct FakeClock {
    ms t { 0 };
    [[nodiscard]] ms now() const {
        return t;
    }
    void advance(ms dt) {
        t += dt;
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
struct SoilSim {
    // Parameters
    double K_percent_per_liter = 0.25;         // % / L
    ms deadTime { std::chrono::seconds { 10 } };     // transport delay
    ms tau { std::chrono::seconds { 20 } };    // time constant
    double evap_percent_per_min = 0.03;        // natural decay when valve off

    struct Input {
        ms t{};
        Liters liters{};
    };
    std::deque<Input> hist{};

    void inject(ms now, Liters liters) {
        if (liters > 0.0)
            hist.push_back({ now, liters });
        // Trim very old inputs
        while (!hist.empty() && (now - hist.front().t) > (deadTime + 10 * tau)) {
            hist.pop_front();
        }
    }

    // Advance one tick
    void step(ms now, Percent& moisture, ms dt) const {
        // Evaporative drift (approximate: linear decay)
        const double dt_min = static_cast<double>(dt.count()) / 60000.0;
        moisture = std::max(0.0, moisture - (evap_percent_per_min * dt_min));

        // Aggregate rise from all past inputs after dead-time
        double dm_total = 0.0;
        for (auto const& u : hist) {
            if (now <= u.t + deadTime)
                continue;    // not arrived yet
            const double age_ms = static_cast<double>((now - (u.t + deadTime)).count());
            const double tau_ms = static_cast<double>(tau.count());
            const double rise = 1.0 - std::exp(-age_ms / tau_ms);    // 0..1
            dm_total += K_percent_per_liter * u.liters * rise;       // % contribution
        }

        // Crude discrete application so moisture approaches the simulated target smoothly
        // Scale by dt/tau (bounded) to avoid big jumps on large dt.
        const double scale = std::min(1.0, static_cast<double>(dt.count()) / static_cast<double>(tau.count()));
        moisture = std::min(100.0, moisture + (dm_total * 0.1 * scale));
    }
};

}    // namespace farmhub::utils::irrigation
