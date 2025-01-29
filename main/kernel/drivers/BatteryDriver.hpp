#pragma once

#include <chrono>
#include <limits>

#include <kernel/Pin.hpp>
#include <kernel/Telemetry.hpp>

using farmhub::kernel::PinPtr;

namespace farmhub::kernel::drivers {

/**
 * @brief Time to wait between battery checks.
 */
static constexpr microseconds LOW_POWER_SLEEP_CHECK_INTERVAL = 10s;

[[noreturn]] inline void enterLowPowerDeepSleep() {
    printf("Entering low power deep sleep\n");
    esp_deep_sleep(duration_cast<microseconds>(LOW_POWER_SLEEP_CHECK_INTERVAL).count());
    // Signal to the compiler that we are not returning for real
    abort();
}

class BatteryDriver : public TelemetryProvider {
public:
    virtual float getVoltage() = 0;

protected:
    virtual void populateTelemetry(JsonObject& json) override {
        json["voltage"] = getVoltage();
    }
};

class AnalogBatteryDriver
    : public BatteryDriver {
public:
    AnalogBatteryDriver(InternalPinPtr pin, float voltageDividerRatio)
        : analogPin(pin)
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
