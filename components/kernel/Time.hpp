#pragma once

#include <chrono>

#include <freertos/FreeRTOS.h>

using namespace std::chrono;
using namespace std::chrono_literals;

namespace farmhub::kernel {

using ticks = std::chrono::duration<uint32_t, std::ratio<1, configTICK_RATE_HZ>>;

} // namespace farmhub::kernel
