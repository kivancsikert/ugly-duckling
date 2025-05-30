#pragma once

#include <chrono>

namespace farmhub::kernel {

using ticks = std::chrono::duration<uint32_t, std::ratio<1, configTICK_RATE_HZ>>;

} // namespace farmhub::kernel
