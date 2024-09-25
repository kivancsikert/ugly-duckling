#include <gtest/gtest.h>

// TODO Move this someplace else?
#define configTICK_RATE_HZ 1000

#include <kernel/MovingAverage.hpp>

namespace farmhub::kernel {

class MovingAverageTest : public testing::Test {
public:
};

TEST_F(MovingAverageTest, empty_instance_returns_zero) {
    MovingAverage<double> ma(3);
    EXPECT_EQ(0, ma.getAverage());
}

TEST_F(MovingAverageTest, single_measurement_is_returned) {
    MovingAverage<double> ma(3);
    ma.record(1);
    EXPECT_EQ(1, ma.getAverage());
}

TEST_F(MovingAverageTest, two_measurements_are_averaged) {
    MovingAverage<double> ma(3);
    ma.record(1);
    ma.record(2);
    EXPECT_EQ(1.5, ma.getAverage());
}

TEST_F(MovingAverageTest, at_capacity_measurements_are_averaged) {
    MovingAverage<double> ma(3);
    ma.record(1);
    ma.record(2);
    ma.record(3);
    EXPECT_EQ(2, ma.getAverage());
}

TEST_F(MovingAverageTest, over_capacity_measurements_are_discarded) {
    MovingAverage<double> ma(3);
    ma.record(1);
    ma.record(2);
    ma.record(3);
    ma.record(4);
    ma.record(5);
    EXPECT_EQ(4, ma.getAverage());
}

}    // namespace farmhub::kernel
