#pragma once

#include <chrono>

#include <kernel/Configuration.hpp>

using namespace std::chrono;
using namespace std::chrono_literals;
using namespace farmhub::kernel;

namespace farmhub::peripherals::flow_meter {

class FlowMeterDeviceConfig
    : public ConfigurationSection {
public:
    Property<gpio_num_t> pin { this, "pin", GPIO_NUM_NC };
    Property<double> qFactor { this, "qFactor", 5.0 };
    Property<milliseconds> measurementFrequency { this, "measurementFrequency", 1s };
};

}
