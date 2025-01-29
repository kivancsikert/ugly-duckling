#pragma once

#include <chrono>
#include <limits>

#include <kernel/Pin.hpp>
#include <kernel/Telemetry.hpp>

using farmhub::kernel::PinPtr;

namespace farmhub::kernel::drivers {

struct BatteryParameters {
    const float maximumVoltage;
    /**
     * @brief Do not boot if battery is below this threshold.
     */
    const float bootThreshold;

    /**
     * @brief Shutdown if battery drops below this threshold.
     */
    const float shutdownThreshold;
};

class BatteryDriver {
public:
    BatteryDriver(const BatteryParameters& parameters)
        : parameters(parameters) {
    }

    virtual float getVoltage() = 0;

    const BatteryParameters parameters;
};

class AnalogBatteryDriver
    : public BatteryDriver {
public:
    AnalogBatteryDriver(InternalPinPtr pin, float voltageDividerRatio, const BatteryParameters& parameters)
        : BatteryDriver(parameters)
        , analogPin(pin)
        , voltageDividerRatio(voltageDividerRatio) {
        LOGI("Initializing analog battery driver on pin %s",
            analogPin.getName().c_str());
    }

    float getVoltage() {
        for (int trial = 0; trial < 5; trial++) {
            auto batteryLevel = analogPin.analogRead();
            if (!batteryLevel.has_value()) {
                LOGE("Failed to read battery level");
                continue;
            }
            return batteryLevel.value() * 3.3 / 4096 * voltageDividerRatio;
        }
        return std::numeric_limits<float>::quiet_NaN();
    }

private:
    AnalogPin analogPin;
    const float voltageDividerRatio;
};

}    // namespace farmhub::kernel::drivers
