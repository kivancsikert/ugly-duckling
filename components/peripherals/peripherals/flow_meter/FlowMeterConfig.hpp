#pragma once

#include <chrono>

#include <Configuration.hpp>

using namespace std::chrono;
using namespace std::chrono_literals;
using namespace farmhub::kernel;

namespace farmhub::peripherals::flow_meter {

class FlowMeterDeviceConfig
    : public ConfigurationSection {
public:
    Property<InternalPinPtr> pin { this, "pin" };
    // Default Q factor for YF-S201 flow sensor
    Property<double> qFactor { this, "qFactor", 7.5 };
    Property<milliseconds> measurementFrequency { this, "measurementFrequency", 1s };
};

}    // namespace farmhub::peripherals::flow_meter
