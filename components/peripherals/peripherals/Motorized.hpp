#pragma once

#include <map>

#include <drivers/MotorDriver.hpp>

#include <peripherals/Peripheral.hpp>

using namespace farmhub::kernel::drivers;

namespace farmhub::peripherals {

std::shared_ptr<PwmMotorDriver> findMotor(
    const std::map<std::string, std::shared_ptr<PwmMotorDriver>>& motors,
    const std::string& motorName) {
    // If there's only one motor and no name is specified, use it
    if (motorName.empty() && motors.size() == 1) {
        return motors.begin()->second;
    }
    for (const auto& m : motors) {
        if (m.first == motorName) return m.second;
    }
    throw PeripheralCreationException("failed to find motor: " + motorName);
}

class Motorized {
public:
    explicit Motorized(const std::map<std::string, std::shared_ptr<PwmMotorDriver>>& motors)
        : motors(motors) {
    }

    std::shared_ptr<PwmMotorDriver> findMotor(const std::string& motorName) const {
        return farmhub::peripherals::findMotor(motors, motorName);
    }

private:
    const std::map<std::string, std::shared_ptr<PwmMotorDriver>> motors;
};

}    // namespace farmhub::peripherals
