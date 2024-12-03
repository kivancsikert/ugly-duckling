#pragma once

#include <chrono>

#include <ArduinoJson.h>

#include <kernel/Configuration.hpp>

#include <peripherals/valve/ValveComponent.hpp>
#include <peripherals/valve/ValveSchedule.hpp>
#include <peripherals/valve/ValveScheduler.hpp>

using namespace std::chrono;
using namespace std::chrono_literals;

using namespace farmhub::kernel;

namespace farmhub::peripherals::valve {

enum class ValveControlStrategyType {
    NormallyOpen,
    NormallyClosed,
    Latching
};

class ValveConfig
    : public ConfigurationSection {
public:
    ArrayProperty<ValveSchedule> schedule { this, "schedule" };
};

class ValveDeviceConfig
    : public ConfigurationSection {
public:
    ValveDeviceConfig(ValveControlStrategyType defaultStrategy)
        : strategy(this, "strategy", defaultStrategy) {
    }

    /**
     * @brief The pin to use to control the valve.
     *
     * @details This can be an internal or an external pin. When specified, the motor is ignored.
     */
    Property<PinPtr> pin { this, "pin" };

    /**
     * @brief The name of the motor service to use to control the valve.
     *
     * @details When the pin is specified, this is ignored.
     */
    Property<String> motor { this, "motor" };

    /**
     * @brief The strategy to use to control the motorized valve.
     *
     * @details Ignored when the pin is specified.
     */
    Property<ValveControlStrategyType> strategy;

    /**
     * @brief Duty to use to hold the motorized valve in place.
     *
     * @details This is a percentage from 0 to 100, default is 100%. This is ignored for latching strategies and when the pin is specified.
     */
    Property<double> holdDuty { this, "holdDuty", 100 };    // This is a percentage

    /**
     * @brief Duration to keep the motor running to switch the motorized valve.
     *
     * @details This is in milliseconds, default is 500ms. This is ignored when the pin is specified.
     */
    Property<milliseconds> switchDuration { this, "switchDuration", 500ms };

    ValveControlStrategy* createValveControlStrategy(Motorized* motorOwner) const {
        PinPtr pin = this->pin.get();
        if (pin != nullptr) {
            return new LatchingPinValveControlStrategy(pin);
        }

        PwmMotorDriver& motor = motorOwner->findMotor(this->motor.get());

        auto switchDuration = this->switchDuration.get();
        auto holdDuty = this->holdDuty.get() / 100.0;

        switch (this->strategy.get()) {
            case ValveControlStrategyType::NormallyOpen:
                return new NormallyOpenMotorValveControlStrategy(motor, switchDuration, holdDuty);
            case ValveControlStrategyType::NormallyClosed:
                return new NormallyClosedMotorValveControlStrategy(motor, switchDuration, holdDuty);
            case ValveControlStrategyType::Latching:
                return new LatchingMotorValveControlStrategy(motor, switchDuration, holdDuty);
            default:
                throw PeripheralCreationException("unknown strategy");
        }
    }
};

// JSON: ValveControlStrategyType

bool convertToJson(const ValveControlStrategyType& src, JsonVariant dst) {
    switch (src) {
        case ValveControlStrategyType::NormallyOpen:
            return dst.set("NO");
        case ValveControlStrategyType::NormallyClosed:
            return dst.set("NC");
        case ValveControlStrategyType::Latching:
            return dst.set("latching");
        default:
            LOGE("Unknown strategy: %d",
                static_cast<int>(src));
            return dst.set("NC");
    }
}
void convertFromJson(JsonVariantConst src, ValveControlStrategyType& dst) {
    String strategy = src.as<String>();
    if (strategy == "NO") {
        dst = ValveControlStrategyType::NormallyOpen;
    } else if (strategy == "NC") {
        dst = ValveControlStrategyType::NormallyClosed;
    } else if (strategy == "latching") {
        dst = ValveControlStrategyType::Latching;
    } else {
        LOGE("Unknown strategy: %s",
            strategy.c_str());
        dst = ValveControlStrategyType::NormallyClosed;
    }
}

}    // namespace farmhub::peripherals::valve
