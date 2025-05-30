#pragma once

#include <chrono>
#include <esp_timer.h>

namespace farmhub::kernel {

/**
 *  @brief Monotonic clock based on ESP's esp_timer_get_time()
 *
 *  Time returned has the property of only increasing at a uniform rate.
 */
struct boot_clock {
    using duration = std::chrono::microseconds;
    using rep = duration::rep;
    using period = duration::period;
    using time_point = std::chrono::time_point<boot_clock, duration>;

    static constexpr bool is_steady = true;

    static time_point now() noexcept {
        return time_point(duration(esp_timer_get_time()));
    }

    static time_point zero() noexcept {
        return time_point(duration(0));
    }
};

}    // namespace farmhub::kernel
