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
    bool on { false };
    void set(bool v) {
        on = v;
    }
    [[nodiscard]] bool is_on() const {
        return on;
    }
};

struct FakeFlow {
    Liters bucket { 0.0 };
    Liters read_and_reset_liters() {
        auto r = bucket;
        bucket = 0.0;
        return r;
    }
};

struct FakeMoisture {
    Percent m { 50.0 };
    Percent read_percent() {
        return m;
    }
};

// Simple FOPDT-ish soil simulator (test-only)
struct SoilSim {
    // Parameters
    double K_percent_per_liter = 0.25;         // % / L
    ms Td { std::chrono::seconds { 10 } };     // transport delay
    ms tau { std::chrono::seconds { 20 } };    // time constant
    double evap_percent_per_min = 0.03;        // natural decay when valve off

    struct Input {
        ms t;
        Liters liters;
    };
    std::deque<Input> hist;

    void inject(ms now, Liters liters) {
        if (liters > 0.0)
            hist.push_back({ now, liters });
        // Trim very old inputs
        while (!hist.empty() && (now - hist.front().t) > (Td + 10 * tau)) {
            hist.pop_front();
        }
    }

    // Advance one tick
    void step(ms now, Percent& m, ms dt) {
        // Evaporative drift (approximate: linear decay)
        const double dt_min = static_cast<double>(dt.count()) / 60000.0;
        m = std::max(0.0, m - evap_percent_per_min * dt_min);

        // Aggregate rise from all past inputs after dead-time
        double dm_total = 0.0;
        for (auto const& u : hist) {
            if (now <= u.t + Td)
                continue;    // not arrived yet
            const double age_ms = static_cast<double>((now - (u.t + Td)).count());
            const double tau_ms = static_cast<double>(tau.count());
            const double rise = 1.0 - std::exp(-age_ms / tau_ms);    // 0..1
            dm_total += K_percent_per_liter * u.liters * rise;       // % contribution
        }

        // Crude discrete application so moisture approaches the simulated target smoothly
        // Scale by dt/tau (bounded) to avoid big jumps on large dt.
        const double scale = std::min(1.0, static_cast<double>(dt.count()) / static_cast<double>(tau.count()));
        m = std::min(100.0, m + dm_total * 0.1 * scale);
    }
};

}    // namespace farmhub::utils::irrigation
