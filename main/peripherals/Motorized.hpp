#pragma once

#include <list>

#include <Arduino.h>

#include <kernel/Service.hpp>

#include <kernel/drivers/MotorDriver.hpp>

#include <peripherals/Peripheral.hpp>

using namespace farmhub::kernel::drivers;

namespace farmhub::peripherals {

class Motorized {
public:
    Motorized(const std::list<ServiceRef<PwmMotorDriver>>& motors)
        : motors(motors) {
    }

    PwmMotorDriver& findMotor(const String& motorName) {
        // If there's only one motor and no name is specified, use it
        if (motorName.isEmpty() && motors.size() == 1) {
            return motors.front().get();
        }
        for (auto& motor : motors) {
            if (motor.getName() == motorName) {
                return motor.get();
            }
        }
        throw PeripheralCreationException("failed to find motor: " + motorName);
    }

private:
    const std::list<ServiceRef<PwmMotorDriver>> motors;
};

}    // namespace farmhub::peripherals
