#pragma once

#include "Units.hpp"

namespace farmhub::peripherals::api {

class ISoilMoistureSensor {
public:
    virtual ~ISoilMoistureSensor() = default;
    virtual Percent getMoisture() = 0;    // Returns a raw moisture percentage reading
};

}    // namespace farmhub::peripherals::api
