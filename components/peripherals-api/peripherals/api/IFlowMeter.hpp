#pragma once

#include "Units.hpp"

namespace farmhub::peripherals::api {

class IFlowMeter {
public:
    virtual ~IFlowMeter() = default;
    virtual Liters getVolume() = 0;
};

}    // namespace farmhub::peripherals::api
