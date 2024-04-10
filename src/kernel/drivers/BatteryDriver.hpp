#pragma once

#include <Arduino.h>

#include <kernel/Log.hpp>
#include <kernel/Telemetry.hpp>

namespace farmhub::kernel::drivers {

class BatteryDriver
    : public TelemetryProvider {
public:
    BatteryDriver(gpio_num_t pin, float voltageDividerRatio)
        : pin(pin)
        , voltageDividerRatio(voltageDividerRatio) {
        Log.info("Initializing battery driver on pin %d",
            pin);

        pinMode(pin, INPUT);
    }

    float getVoltage() {
        auto batteryLevel = analogRead(pin);
        return batteryLevel * 3.3 / 4096 * voltageDividerRatio;
    }

protected:
    void populateTelemetry(JsonObject& json) override {
        json["voltage"] = getVoltage();
    }

private:
    const gpio_num_t pin;
    const float voltageDividerRatio;
};

}    // namespace farmhub::kernel::drivers
