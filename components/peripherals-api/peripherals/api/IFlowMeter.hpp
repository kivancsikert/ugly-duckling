#pragma once

#include "IPeripheral.hpp"
#include "Units.hpp"

namespace farmhub::peripherals::api {

struct IFlowMeter : virtual IPeripheral {
    virtual Liters getVolume() = 0;
};

}    // namespace farmhub::peripherals::api
