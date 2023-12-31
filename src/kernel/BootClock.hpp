#pragma once

#include <chrono>
#include <esp_timer.h>

using namespace std;

namespace farmhub::kernel {

/**
 *  @brief Monotonic clock based on ESP's esp_timer_get_time()
 *
 *  Time returned has the property of only increasing at a uniform rate.
 */
struct boot_clock {
    typedef chrono::microseconds duration;
    typedef duration::rep rep;
    typedef duration::period period;
    typedef chrono::time_point<boot_clock, duration> time_point;

    static constexpr bool is_steady = true;

    static time_point now() noexcept {
        return time_point(duration(esp_timer_get_time()));
    }
};

}    // namespace farmhub::kernel
