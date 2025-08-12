#pragma once

#include <map>
#include <memory>
#include <string>

#include <drivers/MotorDriver.hpp>

using namespace farmhub::kernel::drivers;

namespace farmhub::peripherals {

std::shared_ptr<PwmMotorDriver> findMotor(
    const std::map<std::string, std::shared_ptr<PwmMotorDriver>>& motors,
    const std::string& motorName);

}    // namespace farmhub::peripherals
