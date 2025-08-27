#pragma once

#include <chrono>

namespace farmhub::utils {

template <class Rep1, class Period1, class Rep2, class Period2>
double chrono_ratio(
    std::chrono::duration<Rep1, Period1> a,
    std::chrono::duration<Rep2, Period2> b) {
    return std::chrono::duration<double>(a).count() / std::chrono::duration<double>(b).count();
}

}    // namespace farmhub::utils
