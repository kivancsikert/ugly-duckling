#pragma once
#include <algorithm>
#include <chrono>
#include <cmath>
#include <concepts>
#include <cstdint>
#include <functional>
#include <optional>
#include <stdexcept>
#include <string_view>
#include <utility>

#include <peripherals/api/IFlowMeter.hpp>
#include <peripherals/api/ISoilMoistureSensor.hpp>

#include <utils/scheduling/IScheduler.hpp>

using namespace std::chrono_literals;
using namespace farmhub::peripherals::api;

namespace farmhub::utils::scheduling {

// ---------- Strong-ish units ----------
using ms = std::chrono::milliseconds;
using s = std::chrono::seconds;

// ---------- HAL Concepts ----------
template <class T>
concept Clock = requires(const T& clock) {
    // Monotonic time in milliseconds since some epoch. Must not go backwards.
    { clock.now() } -> std::same_as<ms>;
};

// ---------- Config & Telemetry ----------
struct Config {
    // Pulse sizing
    Liters minVolume { 0.5 };
    Liters maxVolume { 10.0 };
    double minGain { 0.05 };    // % per liter (floor)

    // Filters
    double alphaMoisture { 0.30 };    // EMA for moisture
    double alphaSlope { 0.40 };       // EMA for slope

    // Slope thresholds in % / min
    double slopeRise { 0.05 };
    double slopeSettle { 0.01 };

    // Soak timing
    s deadTime { 1min };    // Td
    s tau { 10min };
    s valveTimeout { 5min };

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
    double gain { 0.20 };    // % / L (steady-state gain, K)

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

inline static const char* toString(State state) {
    switch (state) {
        case State::Idle:
            return "Idle";
        case State::Watering:
            return "Watering";
        case State::Soak:
            return "Soak";
        case State::UpdateModel:
            return "UpdateModel";
        case State::Fault:
            return "Fault";
        default:
            return "Unknown";
    }
}

namespace detail {
[[nodiscard]] constexpr double clamp(double x, double lo, double hi) {
    return std::max(lo, std::min(x, hi));
}

constexpr double epsilon = 1e-3;
}    // namespace detail

static constexpr std::optional<ms> getNextDeadline(State state) {
    switch (state) {
        case State::Idle:
        case State::Soak:
            return 30s;
        case State::Watering:
            return 1s;
        case State::UpdateModel:
            // Let's immediately re-assess
            return 0s;
        case State::Fault:
            return {};
        default:
            throw std::invalid_argument("Unknown state");
    }
}

struct MoistureTarget {
    Percent low;
    Percent high;
};

template <Clock TClock>
struct MoistureBasedScheduler : IScheduler {
    MoistureBasedScheduler(
        Config config,
        std::shared_ptr<TClock> clock,
        std::shared_ptr<IFlowMeter> flowMeter,
        std::shared_ptr<ISoilMoistureSensor> moistureSensor)
        : config { config }
        , clock { std::move(clock) }
        , flowMeter { std::move(flowMeter) }
        , moistureSensor { std::move(moistureSensor) } {
        LOGTD(SCHEDULING, "Moisture based scheduler initialized");
    }

    const char* getName() const override {
        return "moisture";
    }

    [[nodiscard]] const Telemetry& getTelemetry() const noexcept {
        return telemetry;
    }
    [[nodiscard]] State getState() const noexcept {
        return state;
    }

    ScheduleResult tick() override {
        // Without a target we do not schedule watering
        if (!target) {
            return {
                .targetState = {},
                .nextDeadline = {},
                .shouldPublishTelemetry = false,
            };
        }
        const auto& target = *this->target;

        const auto now = clock->now();
        sampleAndFilter(now);

        switch (state) {
            case State::Idle:
                decideOrStartWatering(now, target);
                break;
            case State::Watering:
                continueWatering(now);
                break;
            case State::Soak:
                soak(now);
                break;
            case State::UpdateModel:
                updateModel(now);
                break;
            case State::Fault: /* stay here */
                break;
        }

        auto targetState = state == State::Watering ? TargetState::OPEN : TargetState::CLOSED;
        auto nextDeadline = getNextDeadline(state);

        LOGTV(SCHEDULING, "Tick done: state=%s, targetState=%s, nextDeadline=%s",
            toString(state),
            toString(targetState),
            nextDeadline.has_value() ? std::to_string(nextDeadline->count()).c_str() : "none");

        return {
            .targetState = targetState,
            .nextDeadline = nextDeadline,
            .shouldPublishTelemetry = false,
        };
    }

    void setTarget(std::optional<MoistureTarget> target) {
        this->target = target;
    }

    void resetTotals() {
        telemetry.totalVolume = 0.0;
        telemetry.totalCycles = 0;
    }

private:
    Config config;
    std::optional<MoistureTarget> target;
    Telemetry telemetry {};

    std::shared_ptr<TClock> clock;
    std::shared_ptr<IFlowMeter> flowMeter;
    std::shared_ptr<ISoilMoistureSensor> moistureSensor;

    State state { State::Idle };

    // Internal sampling
    std::optional<ms> lastSample;
    Percent lastMoisture { NAN };

    // Pulse bookkeeping
    Liters volumePlanned { 0.0 };
    Liters volumeDelivered { 0.0 };
    ms waterStartTime { 0ms };
    ms pulseEndTime { 0ms };
    Percent moistureAtPulseEnd { NAN };
    double slopePeak { 0.0 };
    bool sawRise { false };

    void sampleAndFilter(const ms now) {
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

        LOGTV(SCHEDULING, "Moisture: %.1f%% (raw: %.1f%%), Slope: %.1f%%/min",
            telemetry.moisture, telemetry.rawMoisture, telemetry.slope);
        lastMoisture = telemetry.moisture;
        lastSample = now;
    }

    void decideOrStartWatering(const ms now, const MoistureTarget& target) {
        const Percent targetMid = 0.5 * (target.low + target.high);

        if (telemetry.moisture >= target.low) {
            LOGTV(SCHEDULING, "Moisture OK (%.1f%% >= %.1f%%), no watering needed", telemetry.moisture, target.low);
            return;
        }

        if (telemetry.totalVolume >= config.maxTotalVolume) {
            LOGTD(SCHEDULING, "Water cap reached");
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
            config.minVolume,
            std::min(config.maxVolume, config.maxVolumePerCycle));

        telemetry.lastVolumePlanned = volumePlanned;
        volumeDelivered = 0.0;
        waterStartTime = now;

        LOGTV(SCHEDULING, "Starting watering, planned volume: %.1f L", volumePlanned);

        state = State::Watering;
    }

    void continueWatering(const ms now) {
        volumeDelivered += flowMeter->getVolume();

        const bool reached = volumeDelivered + detail::epsilon >= volumePlanned;
        const bool timeout = (now - waterStartTime) >= config.valveTimeout;

        if (reached || timeout) {
            LOGTD(SCHEDULING, "Watering finished after %.1f L delivered (reason: %s), starting soaking",
                volumeDelivered,
                reached ? "volume reached" : "timeout");
            telemetry.totalVolume += volumeDelivered;
            telemetry.totalCycles += 1;
            telemetry.lastVolumeDelivered = volumeDelivered;

            pulseEndTime = now;
            moistureAtPulseEnd = telemetry.moisture;
            slopePeak = telemetry.slope;
            sawRise = false;

            state = State::Soak;
        } else {
            LOGTV(SCHEDULING, "Watering in progress, %.1f / %.1f L delivered so far", volumeDelivered, volumePlanned);
        }
    }

    void soak(const ms now) {
        const auto timeSincePulseEnd = now - pulseEndTime;

        if (timeSincePulseEnd < config.deadTime) {
            LOGTV(SCHEDULING, "Soaking, waiting for dead time (%lld / %lld s elapsed)",
                duration_cast<seconds>(timeSincePulseEnd).count(),
                duration_cast<seconds>(config.deadTime).count());
            return;
        }

        // Wait for rise first
        if (!sawRise) {
            if (telemetry.slope > config.slopeRise) {
                LOGTD(SCHEDULING, "Rise detected after %lld s and %.1f L, continuing",
                    duration_cast<seconds>(timeSincePulseEnd).count(), volumeDelivered);
                sawRise = true;
                slopePeak = std::max(slopePeak, telemetry.slope);
            } else {
                LOGTV(SCHEDULING, "No rise detected yet after %lld s and %.1f L (%.1f < %.1f)",
                    duration_cast<seconds>(timeSincePulseEnd).count(), volumeDelivered, telemetry.slope, config.slopeRise);
            }
            if (timeSincePulseEnd > config.tau) {
                LOGTD(SCHEDULING, "Assuming settled after %lld s and %.1f L, updating model",
                    duration_cast<seconds>(timeSincePulseEnd).count(), volumeDelivered);
                state = State::UpdateModel;    // give up waiting
            }
            return;
        }

        // After rise, wait for settle
        if (telemetry.slope < config.slopeSettle) {
            LOGTD(SCHEDULING, "Settled after %lld s and %.1f L, updating model",
                duration_cast<seconds>(timeSincePulseEnd).count(), volumeDelivered);
            state = State::UpdateModel;
        } else if (timeSincePulseEnd > config.tau) {
            LOGTD(SCHEDULING, "Hasn't fully settled after %lld s and %.1f L, updating model",
                duration_cast<seconds>(timeSincePulseEnd).count(), volumeDelivered);
            state = State::UpdateModel;
        }
    }

    void updateModel(const ms /*now*/) {
        const double dMoisture = telemetry.moisture - moistureAtPulseEnd;
        const double dVolume = std::max(volumeDelivered, detail::epsilon);

        // Update gain if meaningful change
        if (dMoisture > 0.2) {
            const double observedGain = dMoisture / dVolume;    // % per liter, K_obs
            telemetry.gain = (1.0 - config.betaGain) * telemetry.gain + config.betaGain * observedGain;
        }

        if (telemetry.totalVolume >= config.maxTotalVolume) {
            LOGTD(SCHEDULING, "Volume cap reached mid-process");
            state = State::Fault;
        } else {
            // Next tick will re-plan a (likely smaller) pulse if needed
            LOGTV(SCHEDULING, "Re-planning pulse (%.1f L delivered)", volumeDelivered);
            state = State::Idle;
        }
    }
};

}    // namespace farmhub::utils::scheduling
