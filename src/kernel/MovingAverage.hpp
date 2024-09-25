#pragma once

#include <deque>

namespace farmhub::kernel {

template <typename T>
class MovingAverage {
public:
    MovingAverage(size_t maxMeasurements)
        : maxMeasurements(maxMeasurements) {
    }

    void record(T measurement) {
        while (measurements.size() >= maxMeasurements) {
            sum -= measurements.front();
            measurements.pop_front();
        }
        measurements.emplace_back(measurement);
        sum += measurement;
        average = sum / measurements.size();
    }

    T getAverage() const {
        return average;
    }

private:
    const size_t maxMeasurements;

    std::deque<T> measurements;
    T sum = 0;
    T average = 0;
};

}    // namespace farmhub::kernel
