#pragma once
#include <algorithm>
#include <chrono>
#include <cmath>
#include <concepts>
#include <cstdint>
#include <functional>
#include <optional>
#include <string_view>
#include <utility>

#include <peripherals/api/IFlowMeter.hpp>
#include <peripherals/api/ISoilMoistureSensor.hpp>
#include <peripherals/api/IValve.hpp>

using namespace std::chrono_literals;
using namespace farmhub::peripherals::api;

namespace farmhub::utils::irrigation {

// ---------- Strong-ish units ----------
using ms = std::chrono::milliseconds;
using s = std::chrono::seconds;

// ---------- HAL Concepts ----------
template <class T>
concept Clock = requires(const T& clock) {
    // Monotonic time in milliseconds since some epoch. Must not go backwards.
    { clock.now() } -> std::same_as<ms>;
};

// ---------- Notification hook ----------
using Notifier = std::move_only_function<void(std::string_view)>;

// ---------- Config & Telemetry ----------
struct Config {
    // Targets
    Percent targetLow { 60.0 };
    Percent targetHigh { 80.0 };

    // Pulse sizing
    Liters minV { 0.5 };
    Liters maxV { 10.0 };
    double minGain { 0.05 };    // % per liter (floor)

    // Filters
    double alphaMoisture { 0.30 };    // EMA for moisture
    double alphaSlope { 0.40 };       // EMA for slope

    // Slope thresholds in % / min
    double slopeRise { 0.05 };
    double slopeSettle { 0.01 };

    // Soak timing
    s minDeadTime { std::chrono::minutes { 5 } };
    s maxTau { std::chrono::hours { 1 } };
    s valveTimeout { std::chrono::minutes { 30 } };

    // Learning (EWMA)
    double betaGain { 0.20 };
    double betaDelay { 0.20 };
    double betaTau { 0.20 };

    // Quotas / safety
    Liters maxVolumePerCycle { 30.0 };
    Liters maxTotalVolume { 120.0 };

    // Fault heuristics
    Liters noRiseAfterVolume { 5.0 };
};

struct Telemetry {
    Percent rawMoisture { NAN };
    Percent moisture { NAN };    // filtered
    double slope { 0.0 };        // % / min

    // Learned soil model
    double gain { 0.20 };                          // % / L (steady-state gain, K)
    s deadTime { std::chrono::minutes { 10 } };    // Td
    s tau { std::chrono::minutes { 20 } };

    // Accounting
    Liters totalVolume { 0.0 };
    uint32_t totalCycles { 0 };

    // Pulse bookkeeping
    Liters lastVolumePlanned { 0.0 };
    Liters lastVolumeDelivered { 0.0 };
};

// ---------- Controller ----------
enum class State : uint8_t {
    Idle,
    Watering,
    Soak,
    UpdateModel,
    Fault,
};

namespace detail {
[[nodiscard]] constexpr double clamp(double x, double lo, double hi) {
    return std::max(lo, std::min(x, hi));
}

constexpr double epsilon = 1e-3;
}    // namespace detail

template <Clock TClock>
class MoistureBasedScheduler {
public:
    MoistureBasedScheduler(
        Config config,
        TClock& clock,
        std::shared_ptr<IValve> valve,
        std::shared_ptr<IFlowMeter> flowMeter,
        std::shared_ptr<ISoilMoistureSensor> moistureSensor,
        Notifier notify = nullptr)
        : config { std::move(config) }
        , clock { clock }
        , valve { valve }
        , flowMeter { flowMeter }
        , moistureSensor { moistureSensor }
        , notify { std::move(notify) } {
    }

    [[nodiscard]] const Telemetry& getTelemetry() const noexcept {
        return telemetry;
    }
    [[nodiscard]] State getState() const noexcept {
        return state;
    }

    // Called at a fixed cadence by your task (e.g., every 1–2 seconds).
    void tick() {
        sampleAndFilter();

        switch (state) {
            case State::Idle:
                decideOrStartWatering();
                break;
            case State::Watering:
                continueWatering();
                break;
            case State::Soak:
                soak();
                break;
            case State::UpdateModel:
                updateModel();
                break;
            case State::Fault: /* stay here */
                break;
        }
    }

    // Control surface
    void setTarget(Percent lo, Percent hi) {
        config.targetLow = lo;
        config.targetHigh = hi;
    }

    void resetTotals() {
        telemetry.totalVolume = 0.0;
        telemetry.totalCycles = 0;
    }

private:
    Config config;
    Telemetry telemetry {};

    TClock& clock;
    std::shared_ptr<IValve> valve;
    std::shared_ptr<IFlowMeter> flowMeter;
    std::shared_ptr<ISoilMoistureSensor> moistureSensor;
    Notifier notify;

    State state { State::Idle };

    // Internal sampling
    std::optional<ms> lastSample {};
    Percent lastMoisture { NAN };

    // Pulse bookkeeping
    Liters volumePlanned { 0.0 };
    Liters volumeDelivered { 0.0 };
    ms waterStartTime { 0ms };
    ms pulseEndTime { 0ms };
    Percent moistureAtPulseEnd { NAN };
    double slopePeak { 0.0 };
    bool sawRise { false };

    void sampleAndFilter() {
        const auto now = clock.now();
        if (!lastSample.has_value()) {
            lastSample = now;
        }

        telemetry.rawMoisture = moistureSensor->getMoisture();

        // EMA for moisture
        if (std::isnan(telemetry.moisture)) {
            telemetry.moisture = telemetry.rawMoisture;
        }
        telemetry.moisture = config.alphaMoisture * telemetry.rawMoisture + (1.0 - config.alphaMoisture) * telemetry.moisture;

        // Slope in % per minute
        const auto dtInMillis = (now - *lastSample).count();
        if (dtInMillis > 0) {
            const double dtInFractionalMinutes = static_cast<double>(dtInMillis) / 60000.0;
            const double prev = std::isnan(lastMoisture) ? telemetry.moisture : lastMoisture;
            const double slopeInst = (telemetry.moisture - prev) / (dtInFractionalMinutes > 0.0 ? dtInFractionalMinutes : 1.0);
            telemetry.slope = config.alphaSlope * slopeInst + (1.0 - config.alphaSlope) * telemetry.slope;
        }

        lastMoisture = telemetry.moisture;
        lastSample = now;
    }

    void decideOrStartWatering() {
        const Percent targetMid = 0.5 * (config.targetLow + config.targetHigh);

        if (telemetry.moisture >= config.targetLow) {
            return;
        }

        if (telemetry.totalVolume >= config.maxTotalVolume) {
            notify("Water cap reached");
            state = State::Fault;
            return;
        }

        Percent neededIncrease = detail::clamp(targetMid - telemetry.moisture, 0.0, 100.0);
        double effectiveGain = std::max(telemetry.gain, config.minGain);
        double targetVolume = neededIncrease / effectiveGain;

        // Overshoot protection if slope already positive (rain or prior pulse still rising)
        if (telemetry.slope > config.slopeRise) {
            targetVolume *= 0.5;
        }

        volumePlanned = detail::clamp(
            targetVolume,
            config.minV,
            std::min(config.maxV, config.maxVolumePerCycle));

        telemetry.lastVolumePlanned = volumePlanned;
        volumeDelivered = 0.0;
        waterStartTime = clock.now();

        valve->setState(true);
        state = State::Watering;
    }

    void continueWatering() {
        volumeDelivered += flowMeter->getVolume();

        const bool reached = volumeDelivered + detail::epsilon >= volumePlanned;
        const bool timeout = (clock.now() - waterStartTime) >= config.valveTimeout;

        if (reached || timeout) {
            valve->setState(false);
            telemetry.totalVolume += volumeDelivered;
            telemetry.totalCycles += 1;
            telemetry.lastVolumeDelivered = volumeDelivered;

            pulseEndTime = clock.now();
            moistureAtPulseEnd = telemetry.moisture;
            slopePeak = telemetry.slope;
            sawRise = false;

            state = State::Soak;
        }
    }

    void soak() {
        const auto timeSincePulseEnd = clock.now() - pulseEndTime;
        const auto requiredDeadTime = std::max(config.minDeadTime, telemetry.deadTime);

        if (timeSincePulseEnd < requiredDeadTime) {
            return;
        }

        // Wait for rise first
        if (!sawRise) {
            if (telemetry.slope > config.slopeRise) {
                sawRise = true;
                slopePeak = std::max(slopePeak, telemetry.slope);
            }
            if (timeSincePulseEnd > config.maxTau) {
                state = State::UpdateModel;    // give up waiting
            }
            return;
        }

        // After rise, wait for settle
        if (telemetry.slope < config.slopeSettle || timeSincePulseEnd > config.maxTau) {
            state = State::UpdateModel;
        }
    }

    void updateModel() {
        const double dMoisture = telemetry.moisture - moistureAtPulseEnd;
        const double dVolume = std::max(volumeDelivered, detail::epsilon);

        // Update gain if meaningful change
        if (dMoisture > 0.2) {
            const double observedGain = dMoisture / dVolume;    // % per liter, K_obs
            telemetry.gain = (1.0 - config.betaGain) * telemetry.gain + config.betaGain * observedGain;
        }

        if (telemetry.moisture >= config.targetLow) {
            state = State::Idle;
        } else if (telemetry.totalVolume >= config.maxTotalVolume) {
            notify("Irrigation: daily cap reached mid-process.");
            state = State::Fault;
        } else {
            state = State::Idle;    // next tick will re-plan a (likely smaller) pulse
        }
    }
};

}    // namespace farmhub::utils::irrigation
