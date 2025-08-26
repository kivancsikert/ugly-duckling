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

using namespace std::chrono_literals;

namespace farmhub::utils::irrigation {

// ---------- Strong-ish units ----------
using Percent = float;    // 0..100
using Liters = float;

using ms = std::chrono::milliseconds;
using s = std::chrono::seconds;

// ---------- HAL Concepts ----------
template <class T>
concept Clock = requires(const T& t) {
    // Monotonic time in milliseconds since some epoch. Must not go backwards.
    { t.now() } -> std::same_as<ms>;
};

template <class T>
concept Valve = requires(T& v, const T& cv, bool on) {
    { v.set(on) } -> std::same_as<void>;
    { cv.is_on() } -> std::same_as<bool>;
};

template <class T>
concept FlowMeter = requires(T& fm) {
    // Returns liters accumulated since last call and resets its internal counter.
    { fm.read_and_reset_liters() } -> std::same_as<Liters>;
};

template <class T>
concept MoistureSensor = requires(T& ms) {
    // Returns a raw moisture percentage reading (0..100). Caller should filter.
    { ms.read_percent() } -> std::same_as<Percent>;
};

// ---------- Notification hook ----------
using Notifier = std::move_only_function<void(std::string_view)>;

// ---------- Config & Telemetry ----------
struct Config {
    // Targets
    Percent target_low { 60.0f };
    Percent target_high { 80.0f };

    // Pulse sizing
    Liters V_min { 0.5f };
    Liters V_max { 10.0f };
    float K_min { 0.05f };    // % per liter (floor)

    // Filters
    float alpha_m { 0.30f };    // EMA for moisture
    float alpha_s { 0.40f };    // EMA for slope

    // Slope thresholds in % / min
    float slope_rise { 0.05f };
    float slope_settle { 0.01f };

    // Soak timing
    s Td_min { std::chrono::minutes { 5 } };
    s tau_max { std::chrono::hours { 1 } };
    s valve_timeout { std::chrono::minutes { 30 } };

    // Learning (EWMA)
    float beta_gain { 0.20f };
    float beta_delay { 0.20f };
    float beta_tau { 0.20f };

    // Quotas / safety
    Liters max_liters_per_cycle { 30.0f };
    Liters max_liters_per_day { 120.0f };

    // Fault heuristics
    Liters no_rise_after_L { 5.0f };
};

struct Telemetry {
    Percent m_raw { NAN };
    Percent m { NAN };       // filtered
    float slope { 0.0f };    // % / min

    // Learned soil model
    float K { 0.20f };    // % / L (steady-state gain)
    s Td { std::chrono::minutes { 10 } };
    s tau { std::chrono::minutes { 20 } };

    // Accounting
    Liters liters_today { 0.0f };
    uint32_t cycles_today { 0 };

    // Pulse bookkeeping
    Liters last_V_plan { 0.0f };
    Liters last_V_delivered { 0.0f };
};

// ---------- Controller ----------
enum class State : uint8_t { Idle,
    Watering,
    Soak,
    UpdateModel,
    Fault };

namespace detail {
[[nodiscard]] constexpr float clampf(float x, float lo, float hi) {
    return std::max(lo, std::min(x, hi));
}
}    // namespace detail

template <Clock TClock, Valve TValve, FlowMeter TFlow, MoistureSensor TMoist>
class Controller {
public:
    Controller(Config cfg,
        TClock& clock, TValve& valve, TFlow& flow, TMoist& moist,
        Notifier notify = nullptr)
        : cfg_ { std::move(cfg) }
        , clock_ { clock }
        , valve_ { valve }
        , flow_ { flow }
        , moist_ { moist }
        , notify_ { std::move(notify) } {
    }

    [[nodiscard]] const Telemetry& tel() const noexcept {
        return tel_;
    }
    [[nodiscard]] State state() const noexcept {
        return st_;
    }

    // Called at a fixed cadence by your task (e.g., every 1–2 seconds).
    void tick() {
        sample_and_filter_();

        switch (st_) {
            case State::Idle:
                decide_or_start_watering_();
                break;
            case State::Watering:
                continue_watering_();
                break;
            case State::Soak:
                soak_();
                break;
            case State::UpdateModel:
                update_model_();
                break;
            case State::Fault: /* stay here */
                break;
        }
    }

    // Control surface
    void set_target_band(Percent lo, Percent hi) {
        cfg_.target_low = lo;
        cfg_.target_high = hi;
    }
    void reset_daily_quota() {
        tel_.liters_today = 0.0f;
        tel_.cycles_today = 0;
    }

private:
    Config cfg_;
    Telemetry tel_ {};

    TClock& clock_;
    TValve& valve_;
    TFlow& flow_;
    TMoist& moist_;
    Notifier notify_;

    State st_ { State::Idle };

    // Internal sampling
    std::optional<ms> last_sample_ {};
    Percent last_m_ { NAN };

    // Pulse bookkeeping
    Liters V_plan_ { 0.0f };
    Liters V_delivered_ { 0.0f };
    ms t_water_start_ { 0ms };
    ms t_pulse_end_ { 0ms };
    Percent m_at_pulse_end_ { NAN };
    float slope_peak_ { 0.0f };
    bool saw_rise_ { false };

    // ---- Helpers ----
    [[nodiscard]] ms now_() const {
        return clock_.now();
    }

    void sample_and_filter_() {
        const auto t = now_();
        if (!last_sample_.has_value())
            last_sample_ = t;

        tel_.m_raw = moist_.read_percent();

        // EMA for moisture
        if (std::isnan(tel_.m))
            tel_.m = tel_.m_raw;
        tel_.m = cfg_.alpha_m * tel_.m_raw + (1.f - cfg_.alpha_m) * tel_.m;

        // slope in % per minute
        const auto dt_ms = (t - *last_sample_).count();
        if (dt_ms > 0) {
            const float dt_min = static_cast<float>(dt_ms) / 60000.0f;
            const float prev = std::isnan(last_m_) ? tel_.m : last_m_;
            const float slope_inst = (tel_.m - prev) / (dt_min > 0.f ? dt_min : 1.f);
            tel_.slope = cfg_.alpha_s * slope_inst + (1.f - cfg_.alpha_s) * tel_.slope;
        }

        last_m_ = tel_.m;
        *last_sample_ = t;
    }

    void decide_or_start_watering_() {
        const Percent L = cfg_.target_low;
        const Percent H = cfg_.target_high;
        const Percent mid = 0.5f * (L + H);

        if (tel_.m >= L)
            return;

        if (tel_.liters_today >= cfg_.max_liters_per_day) {
            notify_("Irrigation: daily water cap reached.");
            st_ = State::Fault;
            return;
        }

        float needed = detail::clampf(mid - tel_.m, 0.f, 100.f);
        float K_eff = std::max(tel_.K, cfg_.K_min);
        float V = needed / K_eff;

        // Overshoot protection if slope already positive (rain or prior pulse still rising)
        if (tel_.slope > cfg_.slope_rise)
            V *= 0.5f;

        V_plan_ = detail::clampf(
            V,
            cfg_.V_min,
            std::min(cfg_.V_max, cfg_.max_liters_per_cycle));

        tel_.last_V_plan = V_plan_;
        V_delivered_ = 0.f;
        t_water_start_ = now_();

        valve_.set(true);
        st_ = State::Watering;
    }

    void continue_watering_() {
        V_delivered_ += flow_.read_and_reset_liters();

        const bool reached = V_delivered_ + 1e-3f >= V_plan_;
        const bool timeout = (now_() - t_water_start_) >= cfg_.valve_timeout;

        if (reached || timeout) {
            valve_.set(false);
            tel_.liters_today += V_delivered_;
            tel_.cycles_today += 1;
            tel_.last_V_delivered = V_delivered_;

            t_pulse_end_ = now_();
            m_at_pulse_end_ = tel_.m;
            slope_peak_ = tel_.slope;
            saw_rise_ = false;

            st_ = State::Soak;
        }
    }

    void soak_() {
        const auto since_end = now_() - t_pulse_end_;
        const auto Td_req = std::max(cfg_.Td_min, tel_.Td);

        if (since_end < Td_req)
            return;

        // Wait for rise first
        if (!saw_rise_) {
            if (tel_.slope > cfg_.slope_rise) {
                saw_rise_ = true;
                slope_peak_ = std::max(slope_peak_, tel_.slope);
            }
            if (since_end > cfg_.tau_max) {
                st_ = State::UpdateModel;    // give up waiting
            }
            return;
        }

        // After rise, wait for settle
        if (tel_.slope < cfg_.slope_settle || since_end > cfg_.tau_max) {
            st_ = State::UpdateModel;
        }
    }

    void update_model_() {
        const float dm = tel_.m - m_at_pulse_end_;
        const float dv = std::max(V_delivered_, 1e-3f);

        // Update gain if meaningful change
        if (dm > 0.2f) {
            const float K_obs = dm / dv;    // % per liter
            tel_.K = (1.f - cfg_.beta_gain) * tel_.K + cfg_.beta_gain * K_obs;
        }

        if (tel_.m >= cfg_.target_low) {
            st_ = State::Idle;
        } else if (tel_.liters_today >= cfg_.max_liters_per_day) {
            notify_("Irrigation: daily cap reached mid-process.");
            st_ = State::Fault;
        } else {
            st_ = State::Idle;    // next tick will re-plan a (likely smaller) pulse
        }
    }
};

}    // namespace farmhub::utils::irrigation
