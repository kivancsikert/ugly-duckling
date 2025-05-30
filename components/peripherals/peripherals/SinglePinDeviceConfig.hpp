#pragma once

#include <chrono>

#include <Configuration.hpp>

using namespace std::chrono;
using namespace farmhub::kernel;

namespace farmhub::peripherals {

class SinglePinDeviceConfig
    : public ConfigurationSection {
public:
    Property<InternalPinPtr> pin { this, "pin" };
};

} // namespace farmhub::peripherals
