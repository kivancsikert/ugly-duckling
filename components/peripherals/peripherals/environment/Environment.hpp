#pragma once

#include <concepts>
#include <memory>

#include <Configuration.hpp>
#include <I2CManager.hpp>

#include <peripherals/I2CSettings.hpp>
#include <peripherals/Peripheral.hpp>
#include <utility>

using namespace farmhub::kernel;
using namespace farmhub::kernel::mqtt;
using namespace farmhub::peripherals;

namespace farmhub::peripherals::environment {

class EnvironmentSensor {
public:
    virtual ~EnvironmentSensor() = default;

    virtual double getTemperature() = 0;
    virtual double getMoisture() = 0;
};

}    // namespace farmhub::peripherals::environment
