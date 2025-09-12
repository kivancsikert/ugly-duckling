#pragma once

#include <chrono>
#include <functional>
#include <optional>

#include <BootClock.hpp>
#include <Concurrent.hpp>

using namespace std::chrono_literals;
using namespace farmhub::kernel;

namespace farmhub::utils {

template <typename T>
struct DebouncedParams {
    T& lastValue;
    std::optional<std::chrono::time_point<boot_clock>> lastMeasurement;
};

template <typename T>
class DebouncedMeasurement {
public:
    explicit DebouncedMeasurement(
        std::move_only_function<std::optional<T>(const DebouncedParams<T>)> measure,
        std::chrono::milliseconds interval = 1s,
        const T& defaultValue = {})
        : measure(std::move(measure))
        , interval(interval)
        , value(defaultValue) {
    }

    void updateIfNecessary() {
        Lock lock(mutex);
        auto now = boot_clock::now();
        if (lastMeasurement && now - *lastMeasurement < interval) {
            return;
        }
        auto measurement = measure({
            .lastValue = value,
            .lastMeasurement = lastMeasurement,
        });
        if (measurement) {
            value = *measurement;
            lastMeasurement = now;
        }
    }

    T getValue() {
        updateIfNecessary();
        return value;
    }

private:
    std::move_only_function<std::optional<T>(DebouncedParams<T>)> measure;
    std::chrono::milliseconds interval;

    T value;
    std::optional<std::chrono::time_point<boot_clock>> lastMeasurement;

    Mutex mutex;
};

}    // namespace farmhub::utils
