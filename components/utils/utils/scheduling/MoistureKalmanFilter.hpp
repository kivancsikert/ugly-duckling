#pragma once
#include <array>
#include <cmath>

namespace farmhub::utils::scheduling {
/// @brief Kalman filter to estimate true soil moisture and temperature sensitivity.
/// State vector = [moistReal, beta]^T
/// Measurement model: moistObserved = moistReal + beta * (temp - tempRef) + noise
class MoistureKalmanFilter {
public:
    explicit MoistureKalmanFilter(double initMoistReal = 0.0,
        double initBeta = 0.0,
        double tempRef = 20.0)
        : moistReal(initMoistReal)
        , beta(initBeta)
        , tempRef(tempRef) {
        // Initialize covariance as moderately uncertain
        P = { { { 1.0, 0.0 }, { 0.0, 1.0 } } };
    }

    /// @brief Update filter with new observation
    /// @param moistObserved measured soil moisture (sensor)
    /// @param temp measured soil temperature
    /// @param qMoist process noise for moistReal (slow drift, higher during watering)
    /// @param qBeta process noise for beta (usually tiny)
    /// @param R measurement noise variance
    void update(double moistObserved,
        double temp,
        double qMoist = 1e-5,
        double qBeta = 1e-6,
        double R = 1e-3) {
        // Predict step: state does not change (identity transition)
        // Add process noise
        P[0][0] += qMoist;
        P[1][1] += qBeta;

        // Observation model: H = [1, temp - tempRef]
        double h0 = 1.0;
        double h1 = temp - tempRef;

        // Innovation
        double moistPred = moistReal + beta * h1;
        double innovation = moistObserved - moistPred;

        // Innovation covariance: S = H P H^T + R
        double S = P[0][0] * h0 * h0 + 2 * P[0][1] * h0 * h1 + P[1][1] * h1 * h1 + R;

        // Kalman gain K = P H^T / S
        double K0 = (P[0][0] * h0 + P[0][1] * h1) / S;
        double K1 = (P[1][0] * h0 + P[1][1] * h1) / S;

        // Update state
        moistReal += K0 * innovation;
        beta += K1 * innovation;

        // Update covariance: P = (I - K H) P
        double P00 = P[0][0], P01 = P[0][1], P11 = P[1][1];

        P[0][0] -= K0 * (h0 * P00 + h1 * P01);
        P[0][1] -= K0 * (h0 * P01 + h1 * P11);
        P[1][0] -= K1 * (h0 * P00 + h1 * P01);
        P[1][1] -= K1 * (h0 * P01 + h1 * P11);
    }

    [[nodiscard]] double getMoistReal() const noexcept {
        return moistReal;
    }
    [[nodiscard]] double getBeta() const noexcept {
        return beta;
    }
    [[nodiscard]] double getTempRef() const noexcept {
        return tempRef;
    }

    void setTempRef(double newRef) noexcept {
        tempRef = newRef;
    }

private:
    double moistReal;    // estimated true soil moisture
    double beta;         // estimated temp sensitivity
    double tempRef;      // reference temperature

    // 2x2 covariance matrix
    std::array<std::array<double, 2>, 2> P {};
};

}    // namespace farmhub::utils::scheduling
