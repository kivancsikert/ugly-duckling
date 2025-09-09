#pragma once

#include "IPeripheral.hpp"
#include "Units.hpp"

namespace farmhub::peripherals::api {

struct ITemperatureSensor : virtual IPeripheral {
    virtual Celsius getTemperature() = 0;  // Returns a raw temperature reading
};

}    // namespace farmhub::peripherals::api
