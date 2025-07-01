#pragma once

#include <chrono>
#include <limits>

#include <Pin.hpp>
#include <Telemetry.hpp>
#include <utility>

using farmhub::kernel::PinPtr;

namespace farmhub::kernel::drivers {

struct BatteryParameters {
    /**
     * @brief Maximum voltage of the battery in millivolts.
     *
     */
    const int maximumVoltage;
    /**
     * @brief Do not boot if battery is below this threshold in millivolts.
     */
    const int bootThreshold;

    /**
     * @brief Shutdown if battery drops below this threshold in millivolts.
     */
    const int shutdownThreshold;
};

class BatteryDriver {
public:
    explicit BatteryDriver(const BatteryParameters& parameters)
        : parameters(parameters) {
    }

    virtual ~BatteryDriver() = default;

    /**
     * @brief Get the battery voltage.
     *
     * @return Battery voltage in millivolts, or -1 if the read failed.
     */
    virtual int getVoltage() = 0;

    const BatteryParameters parameters;
};

class AnalogBatteryDriver
    : public BatteryDriver {
public:
    AnalogBatteryDriver(const InternalPinPtr& pin, double voltageDividerRatio, const BatteryParameters& parameters)
        : BatteryDriver(parameters)
        , analogPin(pin)
        , voltageDividerRatio(voltageDividerRatio) {
        LOGI("Initializing analog battery driver on pin %s",
            analogPin.getName().c_str());
    }

    int getVoltage() override {
        for (int trial = 0; trial < 5; trial++) {
            auto batteryLevel = analogPin.analogRead();
            if (!batteryLevel.has_value()) {
                LOGE("Failed to read battery level");
                continue;
            }
            return static_cast<int>(batteryLevel.value() * 3.3 / 4096 * voltageDividerRatio * 1000);
        }
        return -1;
    }

private:
    AnalogPin analogPin;
    const double voltageDividerRatio;
};

}    // namespace farmhub::kernel::drivers
