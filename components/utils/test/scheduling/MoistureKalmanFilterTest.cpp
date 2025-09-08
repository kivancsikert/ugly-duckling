#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <random>

#include <utils/scheduling/MoistureKalmanFilter.hpp>

#include <FakeLog.hpp>

using Catch::Approx;

namespace farmhub::utils::scheduling {

struct SimConfig {
    double moistTrue0 = 80.0;     // initial true moisture
    double betaTrue = 0.20;       // true temp sensitivity (units of moist per °C)
    double tempMean = 20.0;       // °C
    double tempAmp = 3.0;         // diurnal swing amplitude
    double measNoiseSD = 0.02;    // measurement noise SD
};

// Simple simulator for a single time-step
struct StepOut {
    double temp;
    double moistTrue;
    double moistObserved;
};

StepOut stepSim(const SimConfig& cfg,
    double t,
    double moistTruePrev,
    double tempRef,
    std::mt19937& rng) {
    // Temperature: slow sinusoidal variation
    double temp = cfg.tempMean + cfg.tempAmp * std::sin(t / 10.0);

    // True moisture: default = no change (idle)
    double moistTrue = moistTruePrev;

    // Observed moisture = true + beta*(temp - tempRef) + noise
    std::normal_distribution<double> noise(0.0, cfg.measNoiseSD);
    double moistObserved = moistTrue + cfg.betaTrue * (temp - tempRef) + noise(rng);

    return { temp, moistTrue, moistObserved };
}

// Introduce a watering jump to the true moisture
double wateringJump(double moistTruePrev, double jumpAmount) {
    return moistTruePrev + jumpAmount;
}

TEST_CASE("Kalman converges to true beta and moisture in idle", "[kalman][convergence]") {
    SimConfig cfg;
    const double tempRef = 20.0;

    // Filter starts a bit off
    MoistureKalmanFilter filter(/*initMoistReal*/ cfg.moistTrue0 - 2.0,
        /*initBeta*/ 0.00,
        /*tempRef*/ tempRef);

    // Noise settings
    const double qMoistIdle = 1e-5;
    const double qBeta = 1e-6;
    const double R = cfg.measNoiseSD * cfg.measNoiseSD;

    std::mt19937 rng(12345);
    double moistTrue = cfg.moistTrue0;

    // Run for a while to converge
    for (int t = 0; t < 800; ++t) {
        auto s = stepSim(cfg, t, moistTrue, tempRef, rng);
        filter.update(s.moistObserved, s.temp, qMoistIdle, qBeta, R);
    }

    // Expect beta close to truth and moistReal close to true level
    REQUIRE(filter.getBeta() == Approx(cfg.betaTrue).margin(0.01));
    REQUIRE(filter.getMoistReal() == Approx(cfg.moistTrue0).margin(0.20));
}

TEST_CASE("Idle stability: low qMoist yields a steady moistReal", "[kalman][stability]") {
    SimConfig cfg;
    const double tempRef = 20.0;
    MoistureKalmanFilter filter(cfg.moistTrue0, 0.0, tempRef);

    std::mt19937 rng(7);
    const double qMoistIdle = 1e-6;
    const double qBeta = 1e-6;
    const double R = cfg.measNoiseSD * cfg.measNoiseSD;

    double moistTrue = cfg.moistTrue0;
    double minEst = 1e9, maxEst = -1e9;

    for (int t = 0; t < 600; ++t) {
        auto s = stepSim(cfg, t, moistTrue, tempRef, rng);
        filter.update(s.moistObserved, s.temp, qMoistIdle, qBeta, R);
        minEst = std::min(minEst, filter.getMoistReal());
        maxEst = std::max(maxEst, filter.getMoistReal());
    }

    // Should be very steady (range small compared to noise & temp wobble)
    REQUIRE((maxEst - minEst) < 0.6);    // tune threshold as needed
}

TEST_CASE("Watering event: bump qMoist to let moistReal jump quickly", "[kalman][watering]") {
    SimConfig cfg;
    const double tempRef = 20.0;
    MoistureKalmanFilter filter(cfg.moistTrue0, 0.0, tempRef);

    std::mt19937 rng(42);
    const double qMoistIdle = 1e-6;
    const double qMoistWater = 1e-2;    // much larger during watering
    const double qBeta = 1e-6;
    const double R = cfg.measNoiseSD * cfg.measNoiseSD;

    double moistTrue = cfg.moistTrue0;

    // Idle for a while
    for (int t = 0; t < 200; ++t) {
        auto s = stepSim(cfg, t, moistTrue, tempRef, rng);
        filter.update(s.moistObserved, s.temp, qMoistIdle, qBeta, R);
    }

    // Watering jump: +5 units true moisture, let filter react with high qMoist
    moistTrue = wateringJump(moistTrue, /*jump*/ 5.0);
    for (int t = 200; t < 220; ++t) {
        auto s = stepSim(cfg, t, moistTrue, tempRef, rng);
        filter.update(s.moistObserved, s.temp, qMoistWater, qBeta, R);
    }

    // After a short period, estimate should be close to new true level
    REQUIRE(filter.getMoistReal() == Approx(moistTrue).margin(0.8));

    // Back to idle, it should stay near the new baseline
    for (int t = 220; t < 420; ++t) {
        auto s = stepSim(cfg, t, moistTrue, tempRef, rng);
        filter.update(s.moistObserved, s.temp, qMoistIdle, qBeta, R);
    }
    REQUIRE(filter.getMoistReal() == Approx(moistTrue).margin(0.5));
}

TEST_CASE("tempRef usage: consistent estimates around chosen reference", "[kalman][tempRef]") {
    SimConfig cfg;
    std::mt19937 rngA(1), rngB(1);    // same seed for comparable noise

    // Filter 1: tempRef = 20 °C
    MoistureKalmanFilter filterAt20C(cfg.moistTrue0, 0.0, /*tempRef*/ 20.0);
    // Filter 2: tempRef = 0 °C
    MoistureKalmanFilter filterAt0C(cfg.moistTrue0, 0.0, /*tempRef*/ 0.0);

    const double qMoist = 1e-5;
    const double qBeta = 1e-6;
    const double R = cfg.measNoiseSD * cfg.measNoiseSD;

    double moistTrueA = cfg.moistTrue0;
    double moistTrueB = cfg.moistTrue0;

    // Run parallel simulations; both should estimate the same *true* moisture,
    // even though beta values will differ by a constant offset due to tempRef.
    for (int t = 0; t < 800; ++t) {
        auto a = stepSim(cfg, t, moistTrueA, /*tempRef for obs*/ 20.0, rngA);
        auto b = stepSim(cfg, t, moistTrueB, /*tempRef for obs*/ 0.0, rngB);

        filterAt20C.update(a.moistObserved, a.temp, qMoist, qBeta, R);
        filterAt0C.update(b.moistObserved, b.temp, qMoist, qBeta, R);
    }

    // Moisture estimates should be close (within small tolerance)
    REQUIRE(filterAt20C.getMoistReal() == Approx(filterAt0C.getMoistReal()).margin(0.4));

    // Beta values will differ roughly by betaTrue*(20.0 - 0.0) absorbed into moistReal,
    // so we don't compare beta directly here.
}

}    // namespace farmhub::utils::scheduling
