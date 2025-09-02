#pragma once

#include "IPeripheral.hpp"
#include "Units.hpp"

namespace farmhub::peripherals::api {

struct ISoilMoistureSensor : virtual IPeripheral {
    virtual Percent getMoisture() = 0;    // Returns a raw moisture percentage reading
};

}    // namespace farmhub::peripherals::api
