#pragma once

#include <vector>

namespace farmhub::kernel {

template <std::floating_point T>
class MovingAverage {
public:
    explicit MovingAverage(std::size_t maxMeasurements)
        : maxMeasurements(maxMeasurements)
        , measurements(maxMeasurements, T(0))
        , sum(0)
        , average(0) {
    }

    void record(T measurement) {
        if (count == maxMeasurements) {
            sum -= measurements[currentIndex];
        } else {
            ++count;
        }

        measurements[currentIndex] = measurement;
        sum += measurement;
        average = sum / count;

        currentIndex = (currentIndex + 1) % maxMeasurements;
    }

    constexpr T getAverage() const {
        return average;
    }

private:
    const std::size_t maxMeasurements;
    std::vector<T> measurements;
    std::size_t currentIndex{0};
    std::size_t count{0};
    T sum;
    T average;
};

}    // namespace farmhub::kernel
