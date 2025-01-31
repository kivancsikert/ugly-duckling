#pragma once

#include <map>

#include <kernel/drivers/MotorDriver.hpp>

#include <peripherals/Peripheral.hpp>

using namespace farmhub::kernel::drivers;

namespace farmhub::peripherals {

class Motorized {
public:
    Motorized(const std::map<std::string, std::shared_ptr<PwmMotorDriver>>& motors)
        : motors(motors) {
    }

    std::shared_ptr<PwmMotorDriver> findMotor(const std::string& motorName) {
        // If there's only one motor and no name is specified, use it
        if (motorName.empty() && motors.size() == 1) {
            return motors.begin()->second;
        }
        for (auto& motor : motors) {
            if (motor.first == motorName) {
                return motor.second;
            }
        }
        throw PeripheralCreationException("failed to find motor: " + motorName);
    }

private:
    const std::map<std::string, std::shared_ptr<PwmMotorDriver>> motors;
};

}    // namespace farmhub::peripherals
