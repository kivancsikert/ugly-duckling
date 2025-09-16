#pragma once

#include "IPeripheral.hpp"
#include "Units.hpp"

namespace farmhub::peripherals::api {

struct ILightSensor : virtual IPeripheral {
    virtual Lux getLightLevel() = 0;
};

}    // namespace farmhub::peripherals::api
