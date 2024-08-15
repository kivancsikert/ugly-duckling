#pragma once

#include <Arduino.h>

#include <kernel/Log.hpp>
#include <kernel/Telemetry.hpp>

namespace farmhub::kernel::drivers {

class BatteryDriver : public TelemetryProvider {
public:
    virtual float getVoltage() = 0;

protected:
    void populateTelemetry(JsonObject& json) override {
        json["voltage"] = getVoltage();
    }
};

class AnalogBatteryDriver
    : public BatteryDriver {
public:
    AnalogBatteryDriver(gpio_num_t pin, float voltageDividerRatio)
        : pin(pin)
        , voltageDividerRatio(voltageDividerRatio) {
        Log.info("Initializing analog battery driver on pin %d",
            pin);

        pinMode(pin, INPUT);
    }

    float getVoltage() {
        auto batteryLevel = analogRead(pin);
        return batteryLevel * 3.3 / 4096 * voltageDividerRatio;
    }

private:
    const gpio_num_t pin;
    const float voltageDividerRatio;
};

}    // namespace farmhub::kernel::drivers
